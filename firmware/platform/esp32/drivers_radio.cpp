// drivers_radio.cpp - IR (RMT), NFC (PN532 on I2C), WiFi (+ NTP) and Bluetooth LE HID (NimBLE).
#ifdef ARDUINO
#include <Adafruit_PN532.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <WiFi.h>
#include <Wire.h>
#include <driver/rmt.h>
#include <freertos/semphr.h>
#include <time.h>

#include <cstdio>
#include <cstdlib>

#include "board/board_profile.h"
#include "platform/esp32/drivers.h"

namespace esp {

// =============================== IR (RMT, 1 us ticks) ===============================

namespace {

constexpr rmt_channel_t kIrTx = RMT_CHANNEL_0;  // S3: channels 0-3 send, 4-7 receive
constexpr rmt_channel_t kIrRx = RMT_CHANNEL_4;

void addPulse(std::vector<rmt_item32_t>& v, uint16_t mark, uint16_t space) {
  rmt_item32_t it{};
  it.level0 = 1;
  it.duration0 = mark;
  it.level1 = 0;
  it.duration1 = space;
  v.push_back(it);
}

// timings (mark, space, mark, ...) for the protocols the firmware names
std::vector<uint16_t> encode(const hal::IrSignal& s) {
  std::vector<uint16_t> t;
  auto bitsLsb = [&](uint32_t v, int n, uint16_t mark, uint16_t zero, uint16_t one) {
    for (int i = 0; i < n; i++) t.push_back(mark), t.push_back(((v >> i) & 1) ? one : zero);
  };
  if (s.protocol == "NEC" || s.protocol == "Samsung") {
    const bool sam = s.protocol == "Samsung";
    t.push_back(sam ? 4500 : 9000), t.push_back(4500);
    const uint32_t addr = s.address > 0xFF ? s.address : (s.address & 0xFF) | (sam ? (s.address & 0xFF) << 8 : (~s.address & 0xFF) << 8);
    bitsLsb(addr, 16, 560, 560, 1690);
    bitsLsb((s.command & 0xFF) | ((~s.command & 0xFF) << 8), 16, 560, 560, 1690);
    t.push_back(560), t.push_back(10000);
  } else if (s.protocol == "Sony") {
    // SIRC 12-bit: 7 command bits, 5 address bits, LSB first; marks carry the bit
    t.push_back(2400), t.push_back(600);
    const uint32_t v = (s.command & 0x7F) | ((s.address & 0x1F) << 7);
    for (int i = 0; i < 12; i++) t.push_back(((v >> i) & 1) ? 1200 : 600), t.push_back(600);
    t.back() = 10000;
  } else if (s.protocol == "RC5") {
    // Manchester, 889 us half bits: start bits 1 1, toggle 0, 5 address, 6 command
    const uint32_t v = (0b110u << 11) | ((s.address & 0x1F) << 6) | (s.command & 0x3F);
    std::vector<bool> halves;
    for (int i = 13; i >= 0; i--) {
      const bool b = (v >> i) & 1;
      halves.push_back(!b), halves.push_back(b);  // 1 = space then mark
    }
    bool level = halves[0];
    uint16_t run = 0;
    for (bool h : halves) {
      if (h != level) {
        if (level) t.push_back(run);
        else if (!t.empty()) t.push_back(run);
        level = h;
        run = 0;
      }
      run += 889;
    }
    if (level) t.push_back(run);
    if (t.size() % 2) t.push_back(10000);
  } else {
    t = s.raw;
    if (t.size() % 2) t.push_back(10000);
  }
  return t;
}

bool near(uint32_t v, uint32_t want) { return v > want * 7 / 10 && v < want * 13 / 10; }

bool decode(const std::vector<uint16_t>& t, hal::IrSignal& s) {
  // t: mark, space, mark, space ...
  if (t.size() >= 66 && (near(t[0], 9000) || near(t[0], 4500)) && near(t[1], 4500)) {
    uint32_t a = 0, c = 0;
    for (int i = 0; i < 32; i++) {
      const bool one = t[3 + 2 * i] > 1100;
      if (i < 16) a |= (uint32_t)one << i;
      else c |= (uint32_t)one << (i - 16);
    }
    s.protocol = near(t[0], 9000) ? "NEC" : "Samsung";
    s.address = ((a >> 8) & 0xFF) == (~a & 0xFF) ? (a & 0xFF) : a;
    s.command = c & 0xFF;
    return true;
  }
  if (t.size() >= 24 && near(t[0], 2400)) {
    uint32_t v = 0;
    for (int i = 0; i < 12; i++) v |= (uint32_t)(t[2 + 2 * i] > 900) << i;
    s.protocol = "Sony";
    s.command = v & 0x7F;
    s.address = v >> 7;
    return true;
  }
  return false;
}

}  // namespace

void IrDrv::begin() {
  rmt_config_t tx = RMT_DEFAULT_CONFIG_TX((gpio_num_t)board::kIrTx, kIrTx);
  tx.clk_div = 80;  // 1 us
  tx.tx_config.carrier_en = true;
  tx.tx_config.carrier_freq_hz = 38000;
  tx.tx_config.carrier_duty_percent = 33;
  tx.tx_config.idle_output_en = true;
  rmt_config(&tx);
  rmt_driver_install(kIrTx, 0, 0);

  rmt_config_t rx = RMT_DEFAULT_CONFIG_RX((gpio_num_t)board::kIrRx, kIrRx);
  rx.clk_div = 80;
  rx.rx_config.filter_en = true;
  rx.rx_config.filter_ticks_thresh = 100;
  rx.rx_config.idle_threshold = 12000;  // a 12 ms gap ends a frame
  rmt_config(&rx);
  rmt_driver_install(kIrRx, 2048, 0);
  RingbufHandle_t rb = nullptr;
  rmt_get_ringbuf_handle(kIrRx, &rb);
  rb_ = rb;
}

void IrDrv::setListening(bool on) {
  if (on == listening_) return;
  listening_ = on;
  if (on) rmt_rx_start(kIrRx, true);
  else rmt_rx_stop(kIrRx);
}

bool IrDrv::receive(hal::IrSignal& out) {
  if (!listening_ || !rb_) return false;
  size_t len = 0;
  auto* items = (rmt_item32_t*)xRingbufferReceive((RingbufHandle_t)rb_, &len, 0);
  if (!items) return false;
  // the receiver module pulls its output low during a mark
  std::vector<uint16_t> t;
  for (size_t i = 0; i < len / sizeof(rmt_item32_t); i++) {
    if (items[i].duration0) t.push_back(items[i].duration0);
    if (items[i].duration1) t.push_back(items[i].duration1);
  }
  vRingbufferReturnItem((RingbufHandle_t)rb_, items);
  if (t.size() < 4) return false;
  out = hal::IrSignal{};
  if (!decode(t, out)) {
    out.protocol = "RAW";
    out.raw = t;
  }
  return true;
}

bool IrDrv::send(const hal::IrSignal& s) {
  const std::vector<uint16_t> t = encode(s);
  std::vector<rmt_item32_t> items;
  for (size_t i = 0; i + 1 < t.size(); i += 2) addPulse(items, t[i], t[i + 1]);
  if (items.empty()) return false;
  return rmt_write_items(kIrTx, items.data(), (int)items.size(), true) == ESP_OK;
}

// =============================== NFC (PN532) ===============================

namespace {

Adafruit_PN532& pn532() {
  static Adafruit_PN532 p(board::kNfcIrq, board::kNfcRst, &Wire);
  return p;
}

std::string toHex(const uint8_t* d, size_t n) {
  static const char* k = "0123456789ABCDEF";
  std::string s;
  for (size_t i = 0; i < n; i++) s += k[d[i] >> 4], s += k[d[i] & 15];
  return s;
}

const uint8_t kDefaultKey[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

}  // namespace

bool NfcDrv::begin() {
  if (!ok_) {
    pn532().begin();
    ok_ = pn532().getFirmwareVersion() != 0;
    if (ok_) pn532().SAMConfig();
  }
  return ok_;
}

void NfcDrv::setPolling(bool on) {
  polling_ = on;
  if (!on) present_ = false;
}

bool NfcDrv::card(hal::NfcCard& out) {
  if (!ok_ || !polling_) return false;
  // the reader is asked at most every 200 ms; a card is read in full once, when it arrives
  if (::millis() - lastPoll_ >= 200) {
    lastPoll_ = ::millis();
    uint8_t uid[7] = {0};
    uint8_t len = 0;
    const bool seen = pn532().readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &len, 30);
    const std::string u = seen ? toHex(uid, len) : "";
    if (!seen) present_ = false;
    else if (!present_ || u != card_.uid) {
      card_ = hal::NfcCard{};
      card_.uid = u;
      if (len == 4) {
        card_.type = "MIFARE Classic 1K";
        for (int b = 0; b < 64; b++) {
          uint8_t data[16];
          const bool authed = pn532().mifareclassic_AuthenticateBlock(uid, len, b, 0, const_cast<uint8_t*>(kDefaultKey));
          card_.blocks.push_back(authed && pn532().mifareclassic_ReadDataBlock(b, data) ? toHex(data, 16) : std::string(32, '?'));
        }
      } else {
        card_.type = "NTAG215";  // 7-byte UID: an NTAG / Ultralight; only the UID is kept
      }
      present_ = true;
    }
  }
  if (present_) out = card_;
  return present_;
}

bool NfcDrv::writeBlock(int index, const std::string& hex) {
  if (!present_ || index <= 0 || hex.size() < 32) return false;  // block 0 of a normal card is read-only
  uint8_t uid[7];
  const size_t len = card_.uid.size() / 2;
  for (size_t i = 0; i < len && i < 7; i++) uid[i] = (uint8_t)std::strtoul(card_.uid.substr(i * 2, 2).c_str(), nullptr, 16);
  uint8_t data[16];
  for (int i = 0; i < 16; i++) data[i] = (uint8_t)std::strtoul(hex.substr(i * 2, 2).c_str(), nullptr, 16);
  if (!pn532().mifareclassic_AuthenticateBlock(uid, (uint8_t)len, index, 0, const_cast<uint8_t*>(kDefaultKey))) return false;
  return pn532().mifareclassic_WriteDataBlock(index, data);
}

void NfcDrv::startEmulation(const hal::NfcCard& c) {
  // The PN532 can act as a target (TgInitAsTarget) but presents its own 3-byte NFCID1 (08 xx xx), not the
  // dump's UID: readers see "a card", not this card. Counted as reader taps for the UI.
  card_ = c;
  emulating_ = true;
  taps_ = 0;
}

void NfcDrv::stopEmulation() { emulating_ = false; }

// =============================== WiFi ===============================

hal::WifiState WifiDrv::state() const {
  const_cast<WifiDrv*>(this)->tick();
  return state_;
}

void WifiDrv::startScan() {
  if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);
  WiFi.scanNetworks(true);
  state_ = hal::WifiState::Scanning;
}

void WifiDrv::connect(const std::string& ssid, const std::string& password) {
  if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.empty() ? nullptr : password.c_str());
  state_ = hal::WifiState::Connecting;
  ntpStarted_ = false;
}

void WifiDrv::disconnect() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  state_ = hal::WifiState::Off;
}

std::string WifiDrv::connectedSsid() const {
  return state_ == hal::WifiState::Connected ? std::string(WiFi.SSID().c_str()) : std::string();
}

void WifiDrv::tick() {
  if (state_ == hal::WifiState::Scanning) {
    const int n = WiFi.scanComplete();
    if (n >= 0) {
      results_.clear();
      for (int i = 0; i < n; i++)
        results_.push_back({WiFi.SSID(i).c_str(), (int)WiFi.RSSI(i), WiFi.encryptionType(i) != WIFI_AUTH_OPEN});
      WiFi.scanDelete();
      state_ = WiFi.status() == WL_CONNECTED ? hal::WifiState::Connected : hal::WifiState::Idle;
    } else if (n == WIFI_SCAN_FAILED) {
      results_.clear();
      state_ = hal::WifiState::Idle;
    }
    return;
  }
  if (state_ == hal::WifiState::Off) return;
  const wl_status_t st = WiFi.status();
  if (st == WL_CONNECTED) state_ = hal::WifiState::Connected;
  else if (state_ == hal::WifiState::Connecting && (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL)) state_ = hal::WifiState::Failed;
  else if (state_ == hal::WifiState::Connected) state_ = hal::WifiState::Idle;  // dropped
}

bool WifiDrv::ntpTime(hal::DateTime& utc) {
  if (state() != hal::WifiState::Connected) return false;
  if (!ntpStarted_) {
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    ntpStarted_ = true;
  }
  struct tm t;
  if (!getLocalTime(&t, 2000) || t.tm_year < 120) return false;  // waits up to 2 s for the first answer
  utc.year = (uint16_t)(t.tm_year + 1900);
  utc.month = (uint8_t)(t.tm_mon + 1);
  utc.day = (uint8_t)t.tm_mday;
  utc.hour = (uint8_t)t.tm_hour;
  utc.minute = (uint8_t)t.tm_min;
  utc.second = (uint8_t)t.tm_sec;
  return true;
}

// =============================== Bluetooth LE HID ===============================

namespace {

// report 1: keyboard (modifiers, reserved, 6 keys); report 2: consumer control (one 16-bit usage)
const uint8_t kReportMap[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01, 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01, 0x75, 0x08, 0x81, 0x01, 0x95, 0x06, 0x75, 0x08, 0x15, 0x00,
    0x25, 0x65, 0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0xC0, 0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85,
    0x02, 0x15, 0x00, 0x26, 0xFF, 0x03, 0x19, 0x00, 0x2A, 0xFF, 0x03, 0x75, 0x10, 0x95, 0x01, 0x81, 0x00, 0xC0,
};

struct Ble {
  NimBLEServer* server = nullptr;
  NimBLEHIDDevice* hid = nullptr;
  NimBLECharacteristic* keyboard = nullptr;
  NimBLECharacteristic* consumer = nullptr;
  volatile hal::BleState state = hal::BleState::Off;
  volatile uint32_t passkey = 0;
  volatile bool answer = false;
  SemaphoreHandle_t confirm = nullptr;
  std::string host;
  uint16_t conn = 0xFFFF;
};
Ble gBle;

class Callbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer*, ble_gap_conn_desc* desc) override {
    gBle.conn = desc->conn_handle;
    gBle.host = NimBLEAddress(desc->peer_ota_addr).toString();
    gBle.state = hal::BleState::Connected;
  }
  void onDisconnect(NimBLEServer*) override {
    gBle.conn = 0xFFFF;
    gBle.host.clear();
    gBle.state = hal::BleState::Advertising;
    NimBLEDevice::startAdvertising();
  }
  // A new host shows a six-digit code: the firmware's dialog answers. Known hosts never get here.
  bool onConfirmPIN(uint32_t pin) override {
    gBle.passkey = pin;
    gBle.state = hal::BleState::PairingRequest;
    const bool got = xSemaphoreTake(gBle.confirm, pdMS_TO_TICKS(25000)) == pdTRUE;
    gBle.state = got && gBle.answer ? hal::BleState::Connected : hal::BleState::Advertising;
    return got && gBle.answer;
  }
};

}  // namespace

hal::BleState BleDrv::state() const { return gBle.state; }

void BleDrv::startAdvertising(const std::string& deviceName) {
  if (gBle.state != hal::BleState::Off) return;
  if (!gBle.server) {
    NimBLEDevice::init(deviceName);
    NimBLEDevice::setSecurityAuth(true, true, true);  // bond, MITM, secure connections
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_YESNO);
    gBle.confirm = xSemaphoreCreateBinary();
    gBle.server = NimBLEDevice::createServer();
    gBle.server->setCallbacks(new Callbacks());
    gBle.hid = new NimBLEHIDDevice(gBle.server);
    gBle.keyboard = gBle.hid->inputReport(1);
    gBle.consumer = gBle.hid->inputReport(2);
    gBle.hid->manufacturer()->setValue("DIY Flipper");
    gBle.hid->pnp(0x02, 0x05AC, 0x820A, 0x0210);
    gBle.hid->hidInfo(0x00, 0x01);
    gBle.hid->reportMap((uint8_t*)kReportMap, sizeof(kReportMap));
    gBle.hid->startServices();
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->setAppearance(HID_KEYBOARD);
    adv->addServiceUUID(gBle.hid->hidService()->getUUID());
  }
  NimBLEDevice::startAdvertising();
  gBle.state = hal::BleState::Advertising;
}

void BleDrv::stop() {
  if (!gBle.server) return;
  if (gBle.conn != 0xFFFF) gBle.server->disconnect(gBle.conn);
  NimBLEDevice::stopAdvertising();
  gBle.state = hal::BleState::Off;
}

void BleDrv::disconnect() {
  if (gBle.server && gBle.conn != 0xFFFF) gBle.server->disconnect(gBle.conn);
}

std::string BleDrv::hostName() const { return gBle.host; }
uint32_t BleDrv::passkey() const { return gBle.passkey; }

void BleDrv::confirmPairing(bool accept) {
  gBle.answer = accept;
  if (gBle.confirm) xSemaphoreGive(gBle.confirm);
}

// Hosts are named by their address: a bond keeps no device name. NimBLE drops the oldest bond when the
// store is full (CONFIG_BT_NIMBLE_MAX_BONDS in platformio.ini).
std::vector<hal::BleBond> BleDrv::bonds() const {
  std::vector<hal::BleBond> out;
  if (!gBle.server) return out;
  const int n = NimBLEDevice::getNumBonds();
  for (int i = n - 1; i >= 0; i--) {  // newest first
    const std::string a = NimBLEDevice::getBondedAddress(i).toString();
    out.push_back({a, a});
  }
  return out;
}

void BleDrv::forget(const std::string& host) {
  if (!gBle.server) return;
  for (int i = 0; i < NimBLEDevice::getNumBonds(); i++) {
    const NimBLEAddress a = NimBLEDevice::getBondedAddress(i);
    if (a.toString() != host) continue;
    if (gBle.host == host) disconnect();
    NimBLEDevice::deleteBond(a);
    return;
  }
}

bool BleDrv::sendKey(const hal::HidKey& k) {
  if (gBle.state != hal::BleState::Connected) return false;
  if (k.page == hal::HidKey::Keyboard) {
    uint8_t r[8] = {0, 0, (uint8_t)k.usage, 0, 0, 0, 0, 0};
    gBle.keyboard->setValue(r, sizeof(r));
    gBle.keyboard->notify();
    uint8_t z[8] = {0};
    gBle.keyboard->setValue(z, sizeof(z));
    gBle.keyboard->notify();
  } else {
    uint8_t r[2] = {(uint8_t)(k.usage & 0xFF), (uint8_t)(k.usage >> 8)};
    gBle.consumer->setValue(r, sizeof(r));
    gBle.consumer->notify();
    uint8_t z[2] = {0, 0};
    gBle.consumer->setValue(z, sizeof(z));
    gBle.consumer->notify();
  }
  return true;
}

}  // namespace esp

#endif  // ARDUINO

// drivers_basic.cpp - display, backlight, buttons, flash + SD, battery, RTC, buzzer, power.
#ifdef ARDUINO
#include <algorithm>
#include <cstdio>

#include <Adafruit_ST7735.h>
#include <FS.h>
#include <LittleFS.h>
#include <RTClib.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>
#include <esp_sleep.h>
#include <driver/gpio.h>

#include "board/board_profile.h"
#include "platform/esp32/drivers.h"

namespace esp {

namespace {

constexpr int kBacklightChannel = 0;  // LEDC channels: 0 backlight, 1 buzzer
constexpr int kBuzzerChannel = 1;

Adafruit_ST7735& tft() {
  static Adafruit_ST7735 t(&SPI, board::kTftCs, board::kTftDc, board::kTftRst);
  return t;
}

RTC_DS3231& ds3231() {
  static RTC_DS3231 r;
  return r;
}

RTC_DATA_ATTR char gRetained[64];  // survives deep sleep, lost on power-off

int buttonPin(hal::Button b) {
  switch (b) {
    case hal::Button::Ok: return board::kBtnOk;
    case hal::Button::Cancel: return board::kBtnCancel;
    case hal::Button::Left: return board::kBtnLeft;
    case hal::Button::Right: return board::kBtnRight;
    case hal::Button::Power: return board::kBtnPower;
  }
  return -1;
}

}  // namespace

// ---------------- display ----------------

void DisplayDrv::begin() {
  SPI.begin(board::kSpiSck, board::kSpiMiso, board::kSpiMosi);
  tft().initR(INITR_BLACKTAB);  // if red and blue come out swapped, try INITR_GREENTAB (BRINGUP.md)
  tft().setRotation(0);         // portrait 128x160
  tft().setSPISpeed(board::kDisplaySpiHz);
  tft().fillScreen(0);
}

void DisplayDrv::push(const uint16_t* frame, int16_t x, int16_t y, int16_t w, int16_t h) {
  tft().startWrite();
  tft().setAddrWindow(x, y, w, h);
  for (int16_t row = 0; row < h; row++)
    tft().writePixels(const_cast<uint16_t*>(frame + (y + row) * 128 + x), (uint32_t)w, true, false);
  tft().endWrite();
}

// ---------------- backlight ----------------

void BacklightDrv::begin() {
  ledcSetup(kBacklightChannel, board::kBacklightPwmHz, 8);
  ledcAttachPin(board::kTftBlk, kBacklightChannel);
  ledcWrite(kBacklightChannel, 255);
}

void BacklightDrv::set(uint8_t level) { ledcWrite(kBacklightChannel, level); }

// ---------------- buttons ----------------

void InputDrv::begin() {
  for (int i = 0; i < hal::kButtonCount; i++)
    pinMode(buttonPin((hal::Button)i), board::kButtonsActiveLow ? INPUT_PULLUP : INPUT_PULLDOWN);
}

bool InputDrv::isDown(hal::Button b) const {
  const int level = digitalRead(buttonPin(b));
  return board::kButtonsActiveLow ? level == LOW : level == HIGH;
}

// ---------------- storage ----------------

namespace {

fs::FS* fsOf(hal::Volume v) { return v == hal::Volume::Sd ? (fs::FS*)&SD : (fs::FS*)&LittleFS; }

void listEntries(fs::FS& fs, const std::string& dir, bool dirs, std::vector<std::string>& names) {
  File d = fs.open(dir.c_str());
  if (!d || !d.isDirectory()) return;
  for (File e = d.openNextFile(); e; e = d.openNextFile()) {
    std::string n = e.name();
    const size_t slash = n.rfind('/');
    if (slash != std::string::npos) n = n.substr(slash + 1);
    if (e.isDirectory() == dirs) names.push_back(n);
  }
  std::sort(names.begin(), names.end());
}

bool removeTree(fs::FS& fs, const std::string& path) {
  File f = fs.open(path.c_str());
  if (!f) return false;
  if (!f.isDirectory()) {
    f.close();
    return fs.remove(path.c_str());
  }
  std::vector<std::string> files, dirs;
  listEntries(fs, path, false, files);
  listEntries(fs, path, true, dirs);
  f.close();
  for (const auto& n : files) fs.remove((path + "/" + n).c_str());
  for (const auto& n : dirs) removeTree(fs, path + "/" + n);
  return fs.rmdir(path.c_str());
}

}  // namespace

void StorageDrv::begin() {
  flashOk_ = LittleFS.begin(true);  // formats an empty partition on first boot
  sdOk_ = SD.begin(board::kSdCs, SPI, 20000000);
  sdCheckedAt_ = ::millis();
}

bool StorageDrv::present(hal::Volume v) const {
  if (v == hal::Volume::Flash) return flashOk_;
  // a card pulled out or put back in: look again at most once a second
  if (::millis() - sdCheckedAt_ > 1000) {
    sdCheckedAt_ = ::millis();
    if (sdOk_ && SD.cardType() == CARD_NONE) {
      SD.end();
      sdOk_ = false;
    } else if (!sdOk_) {
      sdOk_ = SD.begin(board::kSdCs, SPI, 20000000);
    }
  }
  return sdOk_;
}

bool StorageDrv::exists(hal::Volume v, const std::string& p) const { return present(v) && fsOf(v)->exists(p.c_str()); }

bool StorageDrv::read(hal::Volume v, const std::string& p, std::string& out) const {
  if (!present(v)) return false;
  File f = fsOf(v)->open(p.c_str(), FILE_READ);
  if (!f || f.isDirectory()) return false;
  out.resize(f.size());
  const size_t n = f.read((uint8_t*)&out[0], out.size());
  out.resize(n);
  return true;
}

bool StorageDrv::write(hal::Volume v, const std::string& p, const std::string& data) {
  if (!present(v)) return false;
  // parent folders first (FAT needs them; LittleFS's create flag covers it too)
  for (size_t s = p.find('/', 1); s != std::string::npos; s = p.find('/', s + 1)) fsOf(v)->mkdir(p.substr(0, s).c_str());
  File f = fsOf(v)->open(p.c_str(), FILE_WRITE, true);
  if (!f) return false;
  const size_t n = f.write((const uint8_t*)data.data(), data.size());
  f.close();
  return n == data.size();
}

bool StorageDrv::remove(hal::Volume v, const std::string& p) { return present(v) && removeTree(*fsOf(v), p); }

bool StorageDrv::rename(hal::Volume v, const std::string& a, const std::string& b) {
  return present(v) && !fsOf(v)->exists(b.c_str()) && fsOf(v)->rename(a.c_str(), b.c_str());
}

bool StorageDrv::list(hal::Volume v, const std::string& dir, std::vector<std::string>& names) const {
  names.clear();
  if (!present(v)) return false;
  listEntries(*fsOf(v), dir, false, names);
  return fsOf(v)->exists(dir.c_str());
}

bool StorageDrv::listDirs(hal::Volume v, const std::string& dir, std::vector<std::string>& names) const {
  names.clear();
  if (!present(v)) return false;
  listEntries(*fsOf(v), dir, true, names);
  return fsOf(v)->exists(dir.c_str());
}

bool StorageDrv::usage(hal::Volume v, uint64_t& used, uint64_t& total) const {
  if (!present(v)) return false;
  if (v == hal::Volume::Flash) {
    used = LittleFS.usedBytes();
    total = LittleFS.totalBytes();
  } else {
    used = SD.usedBytes();
    total = SD.totalBytes();
  }
  return true;
}

// ---------------- battery ----------------

uint16_t BatteryDrv::readMillivolts() {
  const uint32_t mv = analogReadMilliVolts(board::kBatteryAdc);
  return (uint16_t)(mv * board::kBatteryDividerX100 / 100);
}

// ---------------- RTC ----------------

void RtcDrv::begin() {
  Wire.begin(board::kI2cSda, board::kI2cScl);
  ok_ = ds3231().begin(&Wire);
}

bool RtcDrv::now(hal::DateTime& out) {
  if (!ok_ || ds3231().lostPower()) return false;
  const DateTime t = ds3231().now();
  out.year = t.year();
  out.month = t.month();
  out.day = t.day();
  out.hour = t.hour();
  out.minute = t.minute();
  out.second = t.second();
  return true;
}

bool RtcDrv::set(const hal::DateTime& t) {
  if (!ok_) return false;
  ds3231().adjust(DateTime(t.year, t.month, t.day, t.hour, t.minute, t.second));
  return true;
}

// ---------------- buzzer ----------------

void BuzzerDrv::begin() {
  ledcSetup(kBuzzerChannel, 2000, 8);
  ledcAttachPin(board::kBuzzer, kBuzzerChannel);
  ledcWrite(kBuzzerChannel, 0);
}

void BuzzerDrv::tone(uint16_t hz, uint16_t ms) {
  ledcWriteTone(kBuzzerChannel, hz);
  until_ = ::millis() + ms;
  on_ = true;
}

void BuzzerDrv::stop() {
  ledcWrite(kBuzzerChannel, 0);
  on_ = false;
}

void BuzzerDrv::tick() {
  if (on_ && (int32_t)(::millis() - until_) >= 0) stop();
}

// ---------------- power ----------------

hal::WakeReason PowerDrv::wakeReason() const {
  switch (esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_EXT0:
    case ESP_SLEEP_WAKEUP_EXT1: return hal::WakeReason::DeepSleep;
    default: return hal::WakeReason::PowerOn;
  }
}

void PowerDrv::lightSleep() {
  const gpio_int_type_t level = board::kButtonsActiveLow ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL;
  gpio_wakeup_enable((gpio_num_t)board::kBtnOk, level);
  gpio_wakeup_enable((gpio_num_t)board::kBtnPower, level);
  esp_sleep_enable_gpio_wakeup();
  esp_light_sleep_start();  // returns when OK or Power is pressed
}

void PowerDrv::deepSleep() {
  // keep what App left in retained() in RTC memory
  std::snprintf(gRetained, sizeof(gRetained), "%s", retainedCopy_.c_str());
  const uint64_t mask = (1ULL << board::kBtnOk) | (1ULL << board::kBtnPower);
  esp_sleep_enable_ext1_wakeup(mask, board::kButtonsActiveLow ? ESP_EXT1_WAKEUP_ANY_LOW : ESP_EXT1_WAKEUP_ANY_HIGH);
  esp_deep_sleep_start();
}

void PowerDrv::powerOff() {
  gRetained[0] = 0;
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_deep_sleep_start();  // nothing wakes it: the power switch has to be cycled
}

void PowerDrv::restart() { ESP.restart(); }

hal::Usb PowerDrv::usb() const {
  if (board::kUsbSense < 0) return hal::Usb::Unknown;
  return digitalRead(board::kUsbSense) ? hal::Usb::Present : hal::Usb::Absent;
}

std::string& PowerDrv::retained() {
  if (retainedCopy_.empty() && gRetained[0]) retainedCopy_ = gRetained;
  return retainedCopy_;
}

}  // namespace esp

#endif  // ARDUINO

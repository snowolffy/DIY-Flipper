// mocks.h - HAL implementations for the emulator. Their setters are only ever called from the command
// executor (commands.h), so the window, the terminal and scripts change them the same way.
#pragma once

#include <cstdint>
#include <deque>
#include <filesystem>
#include <string>
#include <vector>

#include "hal/hal.h"
#include "ui/framebuffer.h"

namespace sim {

class VirtualClock : public hal::Clock {
 public:
  uint32_t millis() const override { return now_; }
  void set(uint32_t now) { now_ = now; }

 private:
  uint32_t now_ = 0;
};

class MockDisplay : public hal::Display {
 public:
  void push(const uint16_t* frame, int16_t x, int16_t y, int16_t w, int16_t h) override;

  const uint16_t* frame() const { return frame_; }
  uint16_t pixel(int x, int y) const { return frame_[y * ui::kScreenW + x]; }
  uint64_t pushes() const { return pushes_; }
  uint32_t lastPushAt() const { return pushTimes_.empty() ? 0 : pushTimes_.back(); }
  uint64_t bytesPushed() const { return bytes_; }
  // Time the SPI bus spends on the last push at the board's clock (board::kDisplaySpiHz).
  double lastPushMs() const { return lastMs_; }
  // Pushes per second over the last second of virtual time.
  int fps(uint32_t now) const;
  void setClock(const hal::Clock* c) { clock_ = c; }
  // FNV-1a 64 over the pixels as 16-bit little-endian values, as 16 hex digits. Same on every machine.
  std::string hash() const;
  // Colour PNG of the panel, each pixel scaled x scale (nearest neighbour). brightness < 255 darkens it
  // like the backlight would (only the GUI does that; shots are taken at full brightness).
  bool writePng(const std::filesystem::path& path, int scale = 1) const;
  std::vector<uint8_t> png(int scale = 1) const;

 private:
  uint16_t frame_[ui::kPixels] = {};
  uint64_t pushes_ = 0, bytes_ = 0;
  double lastMs_ = 0;
  const hal::Clock* clock_ = nullptr;
  std::deque<uint32_t> pushTimes_;
};

class MockBacklight : public hal::Backlight {
 public:
  void set(uint8_t level) override { level_ = level; }
  uint8_t level() const { return level_; }

 private:
  uint8_t level_ = 255;
};

class MockInput : public hal::Input {
 public:
  bool isDown(hal::Button b) const override { return down_[static_cast<int>(b)]; }
  void set(hal::Button b, bool down) { down_[static_cast<int>(b)] = down; }
  bool anyDown() const {
    for (bool d : down_)
      if (d) return true;
    return false;
  }

 private:
  bool down_[hal::kButtonCount] = {};
};

// Flash and SD as real folders: <root>/flash and <root>/sd.
class MockStorage : public hal::Storage {
 public:
  static constexpr uint64_t kFlashTotal = 14ull * 1024 * 1024;  // LittleFS partition of the 16 MB flash
  static constexpr uint64_t kSdTotal = 488ull * 1024 * 1024;

  explicit MockStorage(std::filesystem::path root) : root_(std::move(root)) {}

  bool present(hal::Volume v) const override;
  bool exists(hal::Volume v, const std::string& path) const override;
  bool read(hal::Volume v, const std::string& path, std::string& out) const override;
  bool write(hal::Volume v, const std::string& path, const std::string& data) override;
  bool remove(hal::Volume v, const std::string& path) override;
  bool rename(hal::Volume v, const std::string& from, const std::string& to) override;
  bool list(hal::Volume v, const std::string& dir, std::vector<std::string>& names) const override;
  bool listDirs(hal::Volume v, const std::string& dir, std::vector<std::string>& names) const override;
  bool usage(hal::Volume v, uint64_t& used, uint64_t& total) const override;

  void setSdPresent(bool present) { sdPresent_ = present; }
  void setFailWrites(bool fail) { failWrites_ = fail; }
  bool sdPresent() const { return sdPresent_; }
  bool failWrites() const { return failWrites_; }
  const std::filesystem::path& root() const { return root_; }

  // "sd:/ir/tv.json" or "flash:/settings.ini" -> volume + path. False for anything else.
  static bool parse(const std::string& spec, hal::Volume& v, std::string& path);
  std::filesystem::path hostPath(hal::Volume v, const std::string& path) const;

 private:
  std::filesystem::path root_;
  bool sdPresent_ = true;
  bool failWrites_ = false;
};

class MockBattery : public hal::Battery {
 public:
  uint16_t readMillivolts() override { return mv_; }
  // percent < 0 means "no usable reading" (ADC returns 0).
  void setPercent(int percent);
  int percentSet() const { return pct_; }

 private:
  uint16_t mv_ = 4000;
  int pct_ = 80;
};

class MockRtc : public hal::Rtc {
 public:
  explicit MockRtc(const VirtualClock& clock) : clock_(clock) {}
  bool now(hal::DateTime& out) override;
  bool set(const hal::DateTime& t) override;
  void setMissing(bool missing) { missing_ = missing; }
  bool missing() const { return missing_; }
  static bool parse(const std::string& iso, hal::DateTime& out);  // "2026-09-30T12:45:00"
  static std::string format(const hal::DateTime& t);
  static int64_t epoch(const hal::DateTime& t);
  static hal::DateTime fromEpoch(int64_t e);

 private:
  const VirtualClock& clock_;
  int64_t baseEpoch_ = 1790771100;  // 2026-09-30 12:25:00 (local time, the RTC keeps UTC+7)
  uint32_t baseMillis_ = 0;
  bool missing_ = false;
};

class MockBuzzer : public hal::Buzzer {
 public:
  struct Tone {
    uint32_t at;
    uint16_t hz, ms;
  };
  explicit MockBuzzer(const VirtualClock& clock) : clock_(clock) {}
  void tone(uint16_t hz, uint16_t ms) override;
  void stop() override { until_ = clock_.millis(); }
  // Frequency sounding now, 0 = silent.
  uint16_t sounding() const { return (int32_t)(until_ - clock_.millis()) > 0 ? hz_ : 0; }
  const std::deque<Tone>& history() const { return history_; }  // newest last, at most 20
  int count() const { return count_; }

 private:
  const VirtualClock& clock_;
  uint16_t hz_ = 0;
  uint32_t until_ = 0;
  std::deque<Tone> history_;
  int count_ = 0;
};

enum class PowerState : uint8_t { Awake, LightSleep, DeepSleep, Off };
const char* powerStateName(PowerState s);

class MockPower : public hal::Power {
 public:
  hal::WakeReason wakeReason() const override { return wake_; }
  void lightSleep() override { state_ = PowerState::LightSleep; }
  void deepSleep() override { state_ = PowerState::DeepSleep; }
  void powerOff() override { state_ = PowerState::Off; }
  void restart() override { restartRequested_ = true; }
  bool takeRestart() {
    const bool r = restartRequested_;
    restartRequested_ = false;
    return r;
  }
  hal::Usb usb() const override { return usb_; }
  std::string& retained() override { return retained_; }

  PowerState state() const { return state_; }
  void setState(PowerState s) { state_ = s; }
  void setWakeReason(hal::WakeReason r) { wake_ = r; }
  void setUsb(hal::Usb u) { usb_ = u; }
  bool switchOn() const { return switchOn_; }
  void setSwitch(bool on) { switchOn_ = on; }
  void clearRetained() { retained_.clear(); }

 private:
  PowerState state_ = PowerState::Awake;
  hal::WakeReason wake_ = hal::WakeReason::PowerOn;
  hal::Usb usb_ = hal::Usb::Unknown;  // the board profile has no USB sense pin yet
  std::string retained_;
  bool switchOn_ = true;
  bool restartRequested_ = false;
};

// ---------- radios ----------

class MockIr : public hal::Ir {
 public:
  void setListening(bool on) override { listening_ = on; }
  bool receive(hal::IrSignal& out) override;
  bool send(const hal::IrSignal& s) override;

  // A remote pressed in front of the receiver. Dropped unless the firmware is listening.
  void inject(const hal::IrSignal& s);
  bool listening() const { return listening_; }
  int sentCount() const { return sentCount_; }
  const std::deque<hal::IrSignal>& sent() const { return sent_; }  // newest last, at most 20
  // "9000 4500 560 560 ..." (microseconds, whitespace or comma separated) -> timings
  static bool parseRaw(const std::string& text, std::vector<uint16_t>& out);

 private:
  bool listening_ = false;
  std::deque<hal::IrSignal> inbox_;
  std::deque<hal::IrSignal> sent_;
  int sentCount_ = 0;
};

class MockNfc : public hal::Nfc {
 public:
  bool begin() override { return modulePresent_; }
  void setPolling(bool on) override { polling_ = on; }
  bool card(hal::NfcCard& out) override;
  bool writeBlock(int index, const std::string& hex) override;
  void startEmulation(const hal::NfcCard& c) override;
  void stopEmulation() override { emulating_ = false; }
  int readerTaps() const override { return taps_; }

  void place(const hal::NfcCard& c) {
    card_ = c;
    present_ = true;
  }
  void remove() { present_ = false; }
  // A reader touches the device while it emulates a card.
  void readerTap() {
    if (emulating_) taps_++;
  }
  void setModulePresent(bool p) { modulePresent_ = p; }
  bool present() const { return present_; }
  bool polling() const { return polling_; }
  bool emulating() const { return emulating_; }
  bool modulePresent() const { return modulePresent_; }
  const hal::NfcCard& cardInField() const { return card_; }
  const hal::NfcCard& emulated() const { return emu_; }
  int blocksWritten() const { return written_; }

 private:
  bool modulePresent_ = true;
  bool polling_ = false;
  bool present_ = false;
  hal::NfcCard card_;
  bool emulating_ = false;
  hal::NfcCard emu_;
  int taps_ = 0;
  int written_ = 0;
};

// Scans and connects finish after latencyMs of virtual time, so "Scanning..." screens are testable.
class MockWifi : public hal::Wifi {
 public:
  explicit MockWifi(const VirtualClock& clock) : clock_(clock) {}

  hal::WifiState state() const override { return state_; }
  void startScan() override;
  std::vector<hal::WifiNetwork> scanResults() const override { return results_; }
  void connect(const std::string& ssid, const std::string& password) override;
  void disconnect() override;
  std::string connectedSsid() const override { return state_ == hal::WifiState::Connected ? ssid_ : ""; }
  bool ntpTime(hal::DateTime& utc) override;

  void tick();  // finishes pending scans/connects; the simulator calls it every loop tick
  void setNetworks(std::vector<hal::WifiNetwork> n) { networks_ = std::move(n); }
  const std::vector<hal::WifiNetwork>& networks() const { return networks_; }
  void setNextConnectSucceeds(bool ok) { nextOk_ = ok; }
  bool nextConnectSucceeds() const { return nextOk_; }
  void setLatencyMs(uint32_t ms) { latencyMs_ = ms; }
  uint32_t latencyMs() const { return latencyMs_; }
  void setNtpResponds(bool r) { ntp_ = r; }
  // the access point goes away: Connected -> Idle
  void drop() {
    if (state_ == hal::WifiState::Connected) state_ = hal::WifiState::Idle, ssid_.clear();
  }
  bool ntpResponds() const { return ntp_; }
  // UTC of the world outside at virtual time 0 (what NTP answers, advancing with the clock)
  void setWorldUtc(int64_t epoch) { worldEpoch_ = epoch; }
  const std::string& lastPassword() const { return password_; }

 private:
  const VirtualClock& clock_;
  hal::WifiState state_ = hal::WifiState::Off;
  std::vector<hal::WifiNetwork> networks_ = {
      {"HomeNet", -48, true}, {"Cafe_Guest", -71, false}, {"True_5G_A1B2", -83, true}};
  std::vector<hal::WifiNetwork> results_;
  std::string ssid_, password_;
  bool nextOk_ = true, ntp_ = true;
  uint32_t latencyMs_ = 800;
  uint32_t doneAt_ = 0;
  int64_t worldEpoch_ = 1791005640;  // 2026-10-03 05:34:00 UTC (12:34 local)
};

class MockBle : public hal::Ble {
 public:
  static constexpr size_t kMaxBonds = 4;

  hal::BleState state() const override { return state_; }
  void startAdvertising(const std::string& deviceName) override;
  void stop() override;
  void disconnect() override;
  std::string hostName() const override { return state_ == hal::BleState::Off ? "" : host_; }
  uint32_t passkey() const override { return passkey_; }
  void confirmPairing(bool accept) override;
  std::vector<hal::BleBond> bonds() const override;
  void forget(const std::string& host) override;
  bool sendKey(const hal::HidKey& key) override;

  // A phone or PC tries to connect. Bonded hosts connect straight away; new ones ask to pair.
  void hostConnect(const std::string& name);
  void hostDisconnect();
  // Fills the bond list with placeholder devices (or clears them) so the next pairing drops the oldest.
  void setBondListFull(bool full);
  const std::string& advertisedName() const { return advName_; }
  int keysSent() const { return keysSent_; }
  const std::deque<hal::HidKey>& keys() const { return keys_; }  // newest last, at most 20

 private:
  hal::BleState state_ = hal::BleState::Off;
  std::string advName_, host_;
  uint32_t passkey_ = 0;
  uint32_t pairings_ = 0;
  std::vector<std::string> bonded_ = {"My Phone", "Laptop", "TV Box"};  // most recent first
  std::deque<hal::HidKey> keys_;
  int keysSent_ = 0;
};

const char* wifiStateName(hal::WifiState s);
const char* bleStateName(hal::BleState s);

}  // namespace sim

// mocks.h - HAL implementations for the simulator. Each one exposes setters that live UI controls and
// script events both call, so the two paths are the same code.
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
  void push(const uint8_t* frame, bool invert) override;

  // What the panel shows: ink bits, flipped when invert is on. 1 = lit pixel.
  bool lit(int16_t x, int16_t y) const;
  bool invert() const { return invert_; }
  const uint8_t* frame() const { return frame_; }
  uint64_t pushes() const { return pushes_; }
  // FNV-1a 64 over the shown pixels, as 16 hex digits. Stable across platforms.
  std::string hash() const;
  bool writePbm(const std::filesystem::path& path) const;

 private:
  uint8_t frame_[ui::kFrameBytes] = {};
  bool invert_ = false;
  uint64_t pushes_ = 0;
};

class MockInput : public hal::Input {
 public:
  bool isDown(hal::Button b) const override { return down_[static_cast<int>(b)]; }
  void set(hal::Button b, bool down) { down_[static_cast<int>(b)] = down; }

 private:
  bool down_[hal::kButtonCount] = {};
};

// Flash and SD as real folders: <root>/flash and <root>/sd.
class MockStorage : public hal::Storage {
 public:
  explicit MockStorage(std::filesystem::path root) : root_(std::move(root)) {}

  bool present(hal::Volume v) const override;
  bool exists(hal::Volume v, const std::string& path) const override;
  bool read(hal::Volume v, const std::string& path, std::string& out) const override;
  bool write(hal::Volume v, const std::string& path, const std::string& data) override;
  bool list(hal::Volume v, const std::string& dir, std::vector<std::string>& names) const override;

  void setSdPresent(bool present) { sdPresent_ = present; }
  void setFailWrites(bool fail) { failWrites_ = fail; }
  bool sdPresent() const { return sdPresent_; }

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
  void setMillivolts(uint16_t mv) { mv_ = mv; }

 private:
  uint16_t mv_ = 4000;
};

class MockRtc : public hal::Rtc {
 public:
  explicit MockRtc(const VirtualClock& clock) : clock_(clock) {}
  bool now(hal::DateTime& out) override;
  // Sets the wall time as of the current virtual millis; it then advances with the clock.
  void set(const hal::DateTime& t);
  void setMissing(bool missing) { missing_ = missing; }
  static bool parse(const std::string& iso, hal::DateTime& out);  // "2026-09-30T12:45:00"

 private:
  const VirtualClock& clock_;
  int64_t baseEpoch_ = 1790771100;  // 2026-09-30 12:25:00 UTC
  uint32_t baseMillis_ = 0;
  bool missing_ = false;
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

 private:
  bool listening_ = false;
  std::deque<hal::IrSignal> inbox_;
  std::deque<hal::IrSignal> sent_;
  int sentCount_ = 0;
};

class MockNfc : public hal::Nfc {
 public:
  void setPolling(bool on) override { polling_ = on; }
  bool card(hal::NfcCard& out) override;

  void place(const hal::NfcCard& c) {
    card_ = c;
    present_ = true;
  }
  void remove() { present_ = false; }
  bool present() const { return present_; }
  bool polling() const { return polling_; }

 private:
  bool polling_ = false;
  bool present_ = false;
  hal::NfcCard card_;
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

  void tick();  // finishes pending scans/connects; the simulator calls it every loop tick
  void setNetworks(std::vector<hal::WifiNetwork> n) { networks_ = std::move(n); }
  std::vector<hal::WifiNetwork>& networks() { return networks_; }
  void setNextConnectSucceeds(bool ok) { nextOk_ = ok; }
  bool nextConnectSucceeds() const { return nextOk_; }
  void setLatencyMs(uint32_t ms) { latencyMs_ = ms; }
  uint32_t latencyMs() const { return latencyMs_; }
  const std::string& lastPassword() const { return password_; }

 private:
  const VirtualClock& clock_;
  hal::WifiState state_ = hal::WifiState::Off;
  std::vector<hal::WifiNetwork> networks_ = {
      {"HomeNet_5G", -48, true}, {"HomeNet", -55, true}, {"CoffeeShop Free", -71, false}, {"Neighbor-2G", -83, true}};
  std::vector<hal::WifiNetwork> results_;
  std::string ssid_, password_;
  bool nextOk_ = true;
  uint32_t latencyMs_ = 800;
  uint32_t doneAt_ = 0;
};

class MockBle : public hal::Ble {
 public:
  static constexpr size_t kMaxBonds = 4;

  hal::BleState state() const override { return state_; }
  void startAdvertising(const std::string& deviceName) override;
  void stop() override { state_ = hal::BleState::Off; }
  std::string hostName() const override { return host_; }
  uint32_t passkey() const override { return passkey_; }
  void confirmPairing(bool accept) override;
  bool sendKey(uint16_t usage) override;

  // A phone or PC tries to connect. Bonded hosts connect straight away; new ones ask to pair, unless
  // the bond list is full.
  void hostConnect(const std::string& name);
  void hostDisconnect();
  // Fills the bond list with placeholder devices (or clears them) so the next new host is refused.
  void setBondListFull(bool full);
  std::vector<std::string>& bonded() { return bonded_; }
  const std::string& advertisedName() const { return advName_; }
  int keysSent() const { return keysSent_; }
  const std::deque<uint16_t>& keys() const { return keys_; }  // newest last, at most 20

 private:
  hal::BleState state_ = hal::BleState::Off;
  std::string advName_, host_;
  uint32_t passkey_ = 0;
  uint32_t pairings_ = 0;
  std::vector<std::string> bonded_ = {"Pixel 8"};
  std::deque<uint16_t> keys_;
  int keysSent_ = 0;
};

}  // namespace sim

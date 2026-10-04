// drivers.h - the ESP32-S3 implementations of hal.h. Every pin and address comes from board/board_profile.h.
// Compiled only by PlatformIO (platformio.ini); the emulator has its own mocks.
//
// STATUS: compiles; has never run on hardware yet (see docs/STATUS.md and docs/BRINGUP.md).
#pragma once
#ifdef ARDUINO

#include <Arduino.h>

#include "hal/hal.h"

namespace esp {

class ClockDrv : public hal::Clock {
 public:
  uint32_t millis() const override { return ::millis(); }
};

class DisplayDrv : public hal::Display {
 public:
  void begin();
  void push(const uint16_t* frame, int16_t x, int16_t y, int16_t w, int16_t h) override;
};

class BacklightDrv : public hal::Backlight {
 public:
  void begin();
  void set(uint8_t level) override;
};

class InputDrv : public hal::Input {
 public:
  void begin();
  bool isDown(hal::Button b) const override;
};

class StorageDrv : public hal::Storage {
 public:
  void begin();
  bool present(hal::Volume v) const override;
  bool exists(hal::Volume v, const std::string& path) const override;
  bool read(hal::Volume v, const std::string& path, std::string& out) const override;
  bool write(hal::Volume v, const std::string& path, const std::string& data) override;
  bool remove(hal::Volume v, const std::string& path) override;
  bool rename(hal::Volume v, const std::string& from, const std::string& to) override;
  bool list(hal::Volume v, const std::string& dir, std::vector<std::string>& names) const override;
  bool listDirs(hal::Volume v, const std::string& dir, std::vector<std::string>& names) const override;
  bool usage(hal::Volume v, uint64_t& used, uint64_t& total) const override;

 private:
  bool flashOk_ = false;
  mutable bool sdOk_ = false;
  mutable uint32_t sdCheckedAt_ = 0;
};

class BatteryDrv : public hal::Battery {
 public:
  uint16_t readMillivolts() override;
};

class RtcDrv : public hal::Rtc {
 public:
  void begin();
  bool now(hal::DateTime& out) override;
  bool set(const hal::DateTime& t) override;

 private:
  bool ok_ = false;
};

class BuzzerDrv : public hal::Buzzer {
 public:
  void begin();
  void tone(uint16_t hz, uint16_t ms) override;
  void stop() override;
  void tick();  // ends a tone when its time is up

 private:
  uint32_t until_ = 0;
  bool on_ = false;
};

class PowerDrv : public hal::Power {
 public:
  hal::WakeReason wakeReason() const override;
  void lightSleep() override;
  void deepSleep() override;
  void powerOff() override;
  void restart() override;
  hal::Usb usb() const override;
  std::string& retained() override;

 private:
  std::string retainedCopy_;
};

class IrDrv : public hal::Ir {
 public:
  void begin();
  void setListening(bool on) override;
  bool receive(hal::IrSignal& out) override;
  bool send(const hal::IrSignal& s) override;

 private:
  bool listening_ = false;
  void* rb_ = nullptr;
};

class NfcDrv : public hal::Nfc {
 public:
  bool begin() override;
  void setPolling(bool on) override;
  bool card(hal::NfcCard& out) override;
  bool writeBlock(int index, const std::string& hex) override;
  void startEmulation(const hal::NfcCard& c) override;
  void stopEmulation() override;
  int readerTaps() const override { return taps_; }

 private:
  bool ok_ = false, polling_ = false, present_ = false, emulating_ = false;
  uint32_t lastPoll_ = 0;
  hal::NfcCard card_;
  int taps_ = 0;
};

class WifiDrv : public hal::Wifi {
 public:
  hal::WifiState state() const override;
  void startScan() override;
  std::vector<hal::WifiNetwork> scanResults() const override { return results_; }
  void connect(const std::string& ssid, const std::string& password) override;
  void disconnect() override;
  std::string connectedSsid() const override;
  bool ntpTime(hal::DateTime& utc) override;
  void tick();

 private:
  mutable hal::WifiState state_ = hal::WifiState::Off;
  std::vector<hal::WifiNetwork> results_;
  bool ntpStarted_ = false;
};

class BleDrv : public hal::Ble {
 public:
  hal::BleState state() const override;
  void startAdvertising(const std::string& deviceName) override;
  void stop() override;
  void disconnect() override;
  std::string hostName() const override;
  uint32_t passkey() const override;
  void confirmPairing(bool accept) override;
  std::vector<hal::BleBond> bonds() const override;
  void forget(const std::string& host) override;
  bool sendKey(const hal::HidKey& key) override;
};

}  // namespace esp

#endif  // ARDUINO

// hal.h - hardware abstraction layer. The firmware's UI and app code talks only to these interfaces;
// the ESP32 build implements them with real drivers, the simulator with mocks. Nothing here may include
// Arduino, GUI or OS headers.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hal {

enum class Button : uint8_t { Ok, Cancel, Left, Right, Power };
constexpr int kButtonCount = 5;

struct DateTime {
  uint16_t year = 2000;
  uint8_t month = 1, day = 1, hour = 0, minute = 0, second = 0;
};

// Milliseconds since boot. Wraps after ~49 days; compare with subtraction, never with <.
class Clock {
 public:
  virtual ~Clock() = default;
  virtual uint32_t millis() const = 0;
};

// Takes a whole 128x160 1-bit frame (2560 bytes, idx=y*128+x, byte=idx/8, bit=idx%8 LSB-first, 1 = ink).
// invert swaps ink/paper at output time; the frame itself never changes.
class Display {
 public:
  virtual ~Display() = default;
  virtual void push(const uint8_t* frame, bool invert) = 0;
};

// Raw button levels. Press/hold/repeat detection is firmware logic (app/buttons.h), so it gets tested too.
class Input {
 public:
  virtual ~Input() = default;
  virtual bool isDown(Button b) const = 0;
};

enum class Volume : uint8_t { Flash, Sd };

// Paths are absolute within a volume, e.g. "/settings.ini" or "/ir/tv.json".
class Storage {
 public:
  virtual ~Storage() = default;
  virtual bool present(Volume v) const = 0;
  virtual bool exists(Volume v, const std::string& path) const = 0;
  virtual bool read(Volume v, const std::string& path, std::string& out) const = 0;
  virtual bool write(Volume v, const std::string& path, const std::string& data) = 0;
  // Names (not paths) of the files directly inside dir, sorted. False if dir doesn't exist.
  virtual bool list(Volume v, const std::string& dir, std::vector<std::string>& names) const = 0;
};

// Battery voltage from the ADC divider, already scaled to cell millivolts. 0 means no usable reading.
class Battery {
 public:
  virtual ~Battery() = default;
  virtual uint16_t readMillivolts() = 0;
};

// DS3231. now() returns false when the RTC is missing or has lost power.
class Rtc {
 public:
  virtual ~Rtc() = default;
  virtual bool now(DateTime& out) = 0;
};

// ---------- radios ----------

// A decoded IR frame. protocol is "NEC", "Samsung", "Sony", "RC5" or "RAW" (raw keeps mark/space timings).
struct IrSignal {
  std::string protocol;
  uint32_t address = 0;
  uint32_t command = 0;
  std::vector<uint16_t> raw;  // microseconds, alternating mark/space; only for RAW
};

class Ir {
 public:
  virtual ~Ir() = default;
  // The receiver only decodes while listening; signals that arrive otherwise are lost, as on the device.
  virtual void setListening(bool on) = 0;
  // True once per decoded signal.
  virtual bool receive(IrSignal& out) = 0;
  virtual bool send(const IrSignal& s) = 0;
};

// PN532. A card stays "present" while it is in the field.
struct NfcCard {
  std::string uid;                  // hex, e.g. "04A2245A6B1C80"
  std::string type;                 // "NTAG215", "MIFARE Classic 1K", ...
  std::vector<std::string> blocks;  // hex per block/page as read
};

class Nfc {
 public:
  virtual ~Nfc() = default;
  virtual void setPolling(bool on) = 0;
  // True while a card is in the field (and polling is on); fills out with what was read.
  virtual bool card(NfcCard& out) = 0;
};

struct WifiNetwork {
  std::string ssid;
  int rssi = -60;  // dBm
  bool secured = true;
};

enum class WifiState : uint8_t { Off, Idle, Scanning, Connecting, Connected, Failed };

class Wifi {
 public:
  virtual ~Wifi() = default;
  virtual WifiState state() const = 0;
  virtual void startScan() = 0;
  // Results of the last finished scan.
  virtual std::vector<WifiNetwork> scanResults() const = 0;
  virtual void connect(const std::string& ssid, const std::string& password) = 0;
  virtual void disconnect() = 0;  // also turns the radio off
  virtual std::string connectedSsid() const = 0;
};

enum class BleState : uint8_t { Off, Advertising, PairingRequest, Connected, BondListFull };

class Ble {
 public:
  virtual ~Ble() = default;
  virtual BleState state() const = 0;
  virtual void startAdvertising(const std::string& deviceName) = 0;
  virtual void stop() = 0;
  virtual std::string hostName() const = 0;
  virtual uint32_t passkey() const = 0;       // six digits to compare during PairingRequest
  virtual void confirmPairing(bool accept) = 0;
  // HID consumer-control usage (0xCD play/pause, 0xB5 next, 0xB6 previous, 0xE9/0xEA volume).
  virtual bool sendKey(uint16_t usage) = 0;
};

struct Hal {
  Clock& clock;
  Display& display;
  Input& input;
  Storage& storage;
  Battery& battery;
  Rtc& rtc;
  Ir& ir;
  Nfc& nfc;
  Wifi& wifi;
  Ble& ble;
};

}  // namespace hal

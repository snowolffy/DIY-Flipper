// hal.h - hardware abstraction layer. The firmware's UI and app code talks only to these interfaces;
// the ESP32-S3 build implements them with real drivers (platform/esp32), the simulator with mocks. Nothing
// here may include Arduino, GUI or OS headers.
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

// 128x160 RGB565 panel. frame is the whole screen (idx = y*128 + x); only the rectangle x, y, w, h changed
// since the last push, so a driver may send just that window.
class Display {
 public:
  virtual ~Display() = default;
  virtual void push(const uint16_t* frame, int16_t x, int16_t y, int16_t w, int16_t h) = 0;
};

// Panel backlight, PWM. 0 = off.
class Backlight {
 public:
  virtual ~Backlight() = default;
  virtual void set(uint8_t level) = 0;
};

// Raw button levels. Tap/hold/repeat detection is firmware logic (app/input.h), so it gets tested too.
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
  // Creates missing parent folders.
  virtual bool write(Volume v, const std::string& path, const std::string& data) = 0;
  // A file, or a folder with everything in it.
  virtual bool remove(Volume v, const std::string& path) = 0;
  virtual bool rename(Volume v, const std::string& from, const std::string& to) = 0;
  // Names (not paths) of the files directly inside dir, sorted. False if dir doesn't exist.
  virtual bool list(Volume v, const std::string& dir, std::vector<std::string>& names) const = 0;
  // Same for the folders directly inside dir.
  virtual bool listDirs(Volume v, const std::string& dir, std::vector<std::string>& names) const = 0;
  // Bytes used and total size of the volume. False when unknown.
  virtual bool usage(Volume v, uint64_t& used, uint64_t& total) const = 0;
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
  virtual bool set(const DateTime& t) = 0;
};

// Passive buzzer (KY-006): one square-wave tone at a time.
class Buzzer {
 public:
  virtual ~Buzzer() = default;
  virtual void tone(uint16_t hz, uint16_t ms) = 0;
  virtual void stop() = 0;
};

enum class WakeReason : uint8_t { PowerOn, DeepSleep, LightSleep };
enum class Usb : uint8_t { Unknown, Absent, Present };

class Power {
 public:
  virtual ~Power() = default;
  // Why the firmware is (re)starting. DeepSleep: woken from deep sleep, RAM was lost but retained() kept.
  virtual WakeReason wakeReason() const = 0;
  // Sleeps until a button wakes the device. Light sleep returns on wake with RAM intact. Deep sleep does
  // not return: the chip restarts with wakeReason() == DeepSleep.
  virtual void lightSleep() = 0;
  virtual void deepSleep() = 0;
  // Deep sleep with no wake source, for a flat battery. Does not return.
  virtual void powerOff() = 0;
  // Software reset (after a factory reset). Does not return on the device.
  virtual void restart() = 0;
  // USB power plugged in. Unknown when the board has no sense pin (see the board profile).
  virtual Usb usb() const = 0;
  // A few bytes that survive deep sleep (RTC memory on the ESP32), lost on power-off.
  virtual std::string& retained() = 0;
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
  bool magic = false;               // "magic" card: block 0 (the UID) can be written
};

class Nfc {
 public:
  virtual ~Nfc() = default;
  // Wakes the PN532. False when it doesn't answer.
  virtual bool begin() = 0;
  virtual void setPolling(bool on) = 0;
  // True while a card is in the field (and polling is on); fills out with what was read.
  virtual bool card(NfcCard& out) = 0;
  // Writes one block of the card in the field. False if no card, or the block can't be written (block 0
  // of a normal card).
  virtual bool writeBlock(int index, const std::string& hex) = 0;
  // Card emulation: the PN532 answers readers as this card until stopped. readerTaps() counts the readers
  // that have read it since emulation started.
  virtual void startEmulation(const NfcCard& c) = 0;
  virtual void stopEmulation() = 0;
  virtual int readerTaps() const = 0;
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
  // Network time (UTC) once connected. False until an NTP answer has arrived.
  virtual bool ntpTime(DateTime& utc) = 0;
};

enum class BleState : uint8_t { Off, Advertising, PairingRequest, Connected };

// HID usage on the Consumer Control page (0xCD play/pause, 0xB5 next, 0xB6 previous, 0xE9/0xEA volume,
// 0xE2 mute) or the Keyboard page (0x4F right, 0x50 left, 0x51 down, 0x52 up, 0x4B PgUp, 0x4E PgDn,
// 0x29 Esc, 0x28 Enter).
struct HidKey {
  enum Page : uint8_t { Consumer, Keyboard } page = Consumer;
  uint16_t usage = 0;
};

class Ble {
 public:
  virtual ~Ble() = default;
  virtual BleState state() const = 0;
  virtual void startAdvertising(const std::string& deviceName) = 0;
  virtual void stop() = 0;          // stops advertising and drops a connection
  virtual void disconnect() = 0;    // drops the host, keeps advertising
  virtual std::string hostName() const = 0;
  virtual uint32_t passkey() const = 0;  // six digits to compare during PairingRequest
  // Accepting bonds the host; when the bond store is full the least recently used bond is dropped.
  virtual void confirmPairing(bool accept) = 0;
  // Bonded hosts, most recently used first.
  virtual std::vector<std::string> bonds() const = 0;
  // Removes a bond; forgetting the connected host disconnects it.
  virtual void forget(const std::string& host) = 0;
  // Press and release one key. False when no host is connected.
  virtual bool sendKey(const HidKey& key) = 0;
};

struct Hal {
  Clock& clock;
  Display& display;
  Backlight& backlight;
  Input& input;
  Storage& storage;
  Battery& battery;
  Rtc& rtc;
  Buzzer& buzzer;
  Power& power;
  Ir& ir;
  Nfc& nfc;
  Wifi& wifi;
  Ble& ble;
};

}  // namespace hal

// board_profile.h - the hardware this firmware targets: parts, pins and bus speeds. The one place these are
// written down: the ESP32-S3 drivers read their pins from here, the emulator shows the same table (part +
// pin per row of its Hardware tab) and costs display pushes with the same SPI clock, and tests/unit_tests.cpp
// checks the pin rules below. Change a pin here and nowhere else.
//
// Pins are a DRAFT until checked against the real boards (see docs/BRINGUP.md).
#pragma once

#include <cstdint>

namespace board {

// ESP32-S3-DevKitC-1 N16R8 (Cybertice M1622): 16 MB flash, 8 MB octal PSRAM.
constexpr const char* kName = "ESP32-S3 N16R8";
constexpr uint32_t kFlashBytes = 16u * 1024 * 1024;
constexpr uint32_t kPsramBytes = 8u * 1024 * 1024;

// ST7735 1.8" 128x160 SPI, separate BLK pin (L0259).
constexpr const char* kDisplayName = "ST7735 128x160 RGB565";
constexpr uint32_t kDisplaySpiHz = 27000000;  // 40 KB frame = ~12 ms on the wire
constexpr uint32_t kMaxFps = 30;              // App never pushes faster than this
constexpr uint32_t kBacklightPwmHz = 5000;

constexpr int kNoPin = -1;

struct Pin {
  const char* signal;  // what the pin does
  const char* part;    // the part it goes to (Cybertice code)
  int gpio;            // kNoPin = not connected
};

// The table. Order is the emulator's display order.
constexpr Pin kPins[] = {
    {"SPI SCK (TFT + SD)", "ST7735 L0259 / SD M0026", 12},
    {"SPI MOSI (TFT + SD)", "ST7735 L0259 / SD M0026", 11},
    {"SPI MISO (SD)", "SD M0026", 13},
    {"TFT CS", "ST7735 L0259", 10},
    {"TFT DC", "ST7735 L0259", 14},
    {"TFT RST", "ST7735 L0259", 21},
    {"TFT BLK (PWM)", "ST7735 L0259", 47},
    {"SD CS", "SD M0026", 16},
    {"I2C SDA (PN532 + DS3231)", "PN532 M0155 / DS3231 M0010", 8},
    {"I2C SCL (PN532 + DS3231)", "PN532 M0155 / DS3231 M0010", 9},
    {"PN532 IRQ", "PN532 M0155", 41},
    {"PN532 RST", "PN532 M0155", 42},
    {"Button OK", "4-button module M1115", 4},
    {"Button Cancel", "4-button module M1115", 5},
    {"Button <", "4-button module M1115", 6},
    {"Button >", "4-button module M1115", 7},
    {"Button Power", "6x6 micro switch E0016", 15},
    {"IR TX", "IR transmitter M0063", 17},
    {"IR RX", "IR receiver M0054", 18},
    {"Buzzer", "KY-006 passive buzzer M1067", 40},
    {"Battery ADC", "100k/100k divider, LiPo P0189", 1},
    {"USB sense (optional)", "divider not fitted yet", kNoPin},
};
constexpr int kPinCount = (int)(sizeof(kPins) / sizeof(kPins[0]));

constexpr int pin(const char* signal) {
  for (int i = 0; i < kPinCount; i++) {
    const char* a = kPins[i].signal;
    const char* b = signal;
    while (*a && *a == *b) a++, b++;
    if (*a == *b) return kPins[i].gpio;
  }
  return kNoPin - 1;  // unknown signal name: the static_asserts below catch typos
}

constexpr int kSpiSck = pin("SPI SCK (TFT + SD)");
constexpr int kSpiMosi = pin("SPI MOSI (TFT + SD)");
constexpr int kSpiMiso = pin("SPI MISO (SD)");
constexpr int kTftCs = pin("TFT CS");
constexpr int kTftDc = pin("TFT DC");
constexpr int kTftRst = pin("TFT RST");
constexpr int kTftBlk = pin("TFT BLK (PWM)");
constexpr int kSdCs = pin("SD CS");
constexpr int kI2cSda = pin("I2C SDA (PN532 + DS3231)");
constexpr int kI2cScl = pin("I2C SCL (PN532 + DS3231)");
constexpr int kNfcIrq = pin("PN532 IRQ");
constexpr int kNfcRst = pin("PN532 RST");
constexpr int kBtnOk = pin("Button OK");
constexpr int kBtnCancel = pin("Button Cancel");
constexpr int kBtnLeft = pin("Button <");
constexpr int kBtnRight = pin("Button >");
constexpr int kBtnPower = pin("Button Power");
constexpr int kIrTx = pin("IR TX");
constexpr int kIrRx = pin("IR RX");
constexpr int kBuzzer = pin("Buzzer");
constexpr int kBatteryAdc = pin("Battery ADC");
constexpr int kUsbSense = pin("USB sense (optional)");
static_assert(kSpiSck >= kNoPin && kUsbSense >= kNoPin && kBatteryAdc >= kNoPin, "pin name typo");

// The 4-button module's output level when pressed is not known yet (M1115): set once measured.
constexpr bool kButtonsActiveLow = true;

// I2C addresses.
constexpr uint8_t kPn532Addr = 0x24;
constexpr uint8_t kDs3231Addr = 0x68;

// Power: LiPo 1000 mAh (P0189) -> toggle switch (E0375) -> MT3608 boost (P0033); TP4056 USB-C charger
// (P0270); battery measured through a 100k/100k divider (x2).
constexpr uint16_t kBatteryDividerX100 = 200;

// ---- pin rules (checked by tests/unit_tests.cpp) ----
// GPIOs the N16R8 module or the devkit already uses: 26-32 SPI flash, 33-37 octal PSRAM, 0/3/45/46
// strapping, 19/20 USB, 43/44 UART0, 38/48 the RGB LED (depends on the devkit revision).
constexpr int kReservedGpio[] = {0, 3, 19, 20, 26, 27, 28, 29, 30, 31, 32, 33, 34,
                                 35, 36, 37, 38, 43, 44, 45, 46, 48};
constexpr int kMaxGpio = 48;
// OK and Power wake the chip from deep sleep: RTC GPIOs 0-21 only.
constexpr int kWakeGpioMax = 21;
// Battery ADC on ADC1 (GPIO 1-10); ADC2 can't be read while WiFi is on.
constexpr int kAdc1Min = 1, kAdc1Max = 10;

}  // namespace board

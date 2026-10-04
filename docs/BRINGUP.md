# Bring-up: from parts to a working device

The firmware for the ESP32-S3 **compiles but has never run on hardware**. Bring the board up one part at a
time, in this order, so each step only adds one unknown. Every pin is in
[`firmware/board/board_profile.h`](../firmware/board/board_profile.h) - check it against the real boards
first and change pins **only there**; `ctest` checks the pin rules (reserved GPIOs, wake pins, ADC1,
no duplicates) after any change.

Tools: PlatformIO (`pip install platformio`), a USB-C cable to the DevKitC's **UART** port, a multimeter.

```
pio run -e esp32s3                 # build
pio run -e esp32s3 -t upload       # flash
pio device monitor                 # serial log at 115200
```

Until every part is connected, the firmware still boots: missing parts show up on the boot status page
(M1) as `FAIL` / `NONE` instead of stopping it.

## 0. The bare board

Connect: nothing but USB. Flash. Expect on serial: `DIY Flipper 0.2.0 on ESP32-S3, PSRAM ... bytes free`
with about 8 MB of PSRAM. If PSRAM shows 0, the module is not an N16R8 (or `memory_type` is wrong in
`platformio.ini`).

## 1. Display + buttons

Connect: ST7735 (L0259) - SCK 12, MOSI 11, CS 10, DC 14, RST 21, BLK 47, VCC 3V3, GND. The 4-button module
(M1115) - OK 4, Cancel 5, < 6, > 7; the Power micro switch (E0016) between GPIO 15 and GND.

Expect: the boot status page, the Pie logo, then the lock screen; buttons move through the menus.

- Red and blue swapped: change `INITR_BLACKTAB` to `INITR_GREENTAB` in `platform/esp32/drivers_basic.cpp`.
- Picture shifted by 1-2 px or garbage at an edge: same place, try `INITR_GREENTAB` / `INITR_REDTAB`.
- Buttons act inverted (everything "pressed" at boot): the module is active-high; set
  `kButtonsActiveLow = false` in the board profile.
- Backlight dark: BLK must be on GPIO 47; Settings > Display > Brightness drives it (PWM 5 kHz).

## 2. Flash (LittleFS)

Nothing to connect. The first boot formats the LittleFS partition (a few seconds). Expect: change a setting,
power-cycle, the setting is still there. Settings > System > Storage shows FLASH used/total (~14 MB total).

## 3. SD card

Connect: SD module (M0026) on the same SPI bus - SCK 12, MOSI 11, MISO 13, CS 16, 5 V or 3V3 per the module.
Card: FAT32. Expect: boot status `SD CARD .... OK`, Storage shows SD size; pull the card - "SD NOT AVAILABLE"
on pages that need it; push it back - it is found again within a second.

## 4. RTC (DS3231)

Connect: DS3231 (M0010) - SDA 8, SCL 9, 3V3, GND; fit the coin cell. Expect: boot status `RTC DS3231 OK`;
set the time in Settings > Date & Time, power-cycle, the clock keeps running.

## 5. Battery

Connect: LiPo (P0189) through the toggle switch (E0375) to the MT3608 (P0033) and the TP4056 (P0270); the
100k/100k divider from the battery + to GPIO 1. Measure the cell with a multimeter and compare with the
percentage in the status bar (table in `firmware/app/battery.h`). A reading that is off by a constant
factor: check the divider; `kBatteryDividerX100` in the board profile.

## 6. Buzzer

Connect: KY-006 (M1067) signal to GPIO 40. Expect: a click on each button press (Settings > Sound > Button
sound) and a beep when an IR signal is sent.

## 7. IR

Connect: transmitter (M0063) to GPIO 17, receiver (M0054) output to GPIO 18. Expect: IR > Learn shows the
protocol of a TV remote (NEC / Samsung / Sony are decoded, anything else is kept RAW); IR > Send replays it.
Point the transmitter at a phone camera to see it flash.

## 8. NFC (PN532)

Connect: PN532 (M0155) in **I2C mode** (DIP switches/jumpers on the module), SDA 8, SCL 9, IRQ 41, RST 42.
Expect: the NFC menu opens (no "NFC MODULE NOT FOUND"); Read shows a card's UID; MIFARE Classic cards with
the default key are dumped block by block. Known limits of the PN532 driver: card emulation presents the
PN532's own 3-byte ID, not the dump's UID; UID writes need a magic card the library can't drive.

## 9. WiFi and Bluetooth

Nothing to connect. Expect: WiFi > Connect lists networks; after connecting, "TIME SYNCED" and the DS3231
shows the right time (UTC+7). Bluetooth: open the Bluetooth menu, pair from a phone ("Pie Controller"),
confirm the code on the device, Remote > Media > Play/Pause controls the phone's player.

## 10. Sleep

Expect: Power (short press) puts the device to sleep the way Settings > Power > Sleep mode says (Deep by
default); OK or Power wakes it, the lock screen comes up if a PIN is set, then the module that was open.
Measure the current in deep sleep with the multimeter in series with the battery.

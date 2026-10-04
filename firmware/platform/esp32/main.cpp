// main.cpp - the ESP32-S3 entry point: drivers from board_profile.h, then the same app::App the emulator
// runs, ticked every 10 ms.
#ifdef ARDUINO
#include <Arduino.h>

#include "app/app.h"
#include "platform/esp32/drivers.h"

namespace {

esp::ClockDrv clockDrv;
esp::DisplayDrv displayDrv;
esp::BacklightDrv backlightDrv;
esp::InputDrv inputDrv;
esp::StorageDrv storageDrv;
esp::BatteryDrv batteryDrv;
esp::RtcDrv rtcDrv;
esp::BuzzerDrv buzzerDrv;
esp::PowerDrv powerDrv;
esp::IrDrv irDrv;
esp::NfcDrv nfcDrv;
esp::WifiDrv wifiDrv;
esp::BleDrv bleDrv;

hal::Hal hal{clockDrv, displayDrv, backlightDrv, inputDrv, storageDrv, batteryDrv, rtcDrv,
             buzzerDrv, powerDrv, irDrv,      nfcDrv,   wifiDrv,  bleDrv};
app::App* theApp = nullptr;  // ~90 KB of frame buffers: on the heap (PSRAM)

}  // namespace

void setup() {
  Serial.begin(115200);
  backlightDrv.begin();
  displayDrv.begin();  // also starts the shared SPI bus
  inputDrv.begin();
  storageDrv.begin();
  rtcDrv.begin();      // also starts I2C (PN532 shares it)
  buzzerDrv.begin();
  irDrv.begin();
  Serial.printf("DIY Flipper %s on %s, PSRAM %u bytes free\n", app::App::kVersion, "ESP32-S3", (unsigned)ESP.getFreePsram());
  theApp = new app::App(hal);
  theApp->begin();
}

void loop() {
  static TickType_t last = xTaskGetTickCount();
  buzzerDrv.tick();
  wifiDrv.tick();
  theApp->tick();
  vTaskDelayUntil(&last, pdMS_TO_TICKS(10));
}

#endif  // ARDUINO

// Host-build stand-in for <Arduino.h>, just enough for the asset headers Flipper UI Studio exports
// (uint8_t tables marked PROGMEM). Only on the include path of simulator/test builds, never the ESP32 build.
#pragma once

#include <cstdint>

#ifndef PROGMEM
#define PROGMEM
#endif

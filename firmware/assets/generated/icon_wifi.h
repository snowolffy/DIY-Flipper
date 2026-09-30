// icon_wifi.h - exported from Flipper UI Studio
// Icon 8x8 - 1bpp, idx=y*cols+x, byte=idx/8, bit=idx%8, LSB-first
// Install: firmware/assets/icon_wifi.h, entry goes in PIC_LIBRARY[] (assets.cpp)
#pragma once

#include <Arduino.h>

const uint8_t PIC_WIFI[8] PROGMEM = {
  0x7e, 0x81, 0x3c, 0x42, 0x18, 0x24, 0x00, 0x18,
};

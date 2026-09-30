// icon_bluetooth.h - exported from Flipper UI Studio
// Icon 8x8 - 1bpp, idx=y*cols+x, byte=idx/8, bit=idx%8, LSB-first
// Install: firmware/assets/icon_bluetooth.h, entry goes in PIC_LIBRARY[] (assets.cpp)
#pragma once

#include <Arduino.h>

const uint8_t PIC_BLUETOOTH[8] PROGMEM = {
  0x08, 0x18, 0x2a, 0x1c, 0x1c, 0x2a, 0x18, 0x08,
};

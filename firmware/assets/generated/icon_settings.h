// icon_settings.h - exported from Flipper UI Studio
// Icon 12x12 - 1bpp, idx=y*cols+x, byte=idx/8, bit=idx%8, LSB-first
// Install: firmware/assets/icon_settings.h, entry goes in PIC_LIBRARY[] (assets.cpp)
#pragma once

#include <Arduino.h>

const uint8_t PIC_SETTINGS[18] PROGMEM = {
  0x00, 0x00, 0x06, 0xf4, 0xc2, 0x3f, 0x9c, 0xe3, 0x70, 0x0e, 0xc7, 0x39, 0xfc, 0x43, 0x2f, 0x60,
  0x00, 0x00,
};

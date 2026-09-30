// icon_battery.h - exported from Flipper UI Studio
// Icon 10x8 - 1bpp, idx=y*cols+x, byte=idx/8, bit=idx%8, LSB-first
// Install: firmware/assets/icon_battery.h, entry goes in PIC_LIBRARY[] (assets.cpp)
#pragma once

#include <Arduino.h>

const uint8_t PIC_BATTERY[10] PROGMEM = {
  0x00, 0xfc, 0x13, 0x78, 0xaf, 0xbd, 0x06, 0xfe, 0x0f, 0x00,
};

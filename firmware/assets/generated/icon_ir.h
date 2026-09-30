// icon_ir.h - exported from Flipper UI Studio
// Icon 12x12 - 1bpp, idx=y*cols+x, byte=idx/8, bit=idx%8, LSB-first
// Install: firmware/assets/icon_ir.h, entry goes in PIC_LIBRARY[] (assets.cpp)
#pragma once

#include <Arduino.h>

const uint8_t PIC_IR[18] PROGMEM = {
  0xf8, 0x41, 0x20, 0xf2, 0x84, 0x10, 0x00, 0x00, 0x06, 0xf0, 0x00, 0x09, 0xf0, 0x00, 0x09, 0x90,
  0x00, 0x0f,
};

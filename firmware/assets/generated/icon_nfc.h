// icon_nfc.h - exported from Flipper UI Studio
// Icon 12x12 - 1bpp, idx=y*cols+x, byte=idx/8, bit=idx%8, LSB-first
// Install: firmware/assets/icon_nfc.h, entry goes in PIC_LIBRARY[] (assets.cpp)
#pragma once

#include <Arduino.h>

const uint8_t PIC_NFC[18] PROGMEM = {
  0x00, 0xf0, 0x0f, 0x81, 0xd4, 0x89, 0x95, 0xda, 0xa9, 0x81, 0x18, 0x48, 0xbd, 0x10, 0x08, 0xff,
  0x00, 0x00,
};

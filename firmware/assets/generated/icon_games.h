// icon_games.h - exported from Flipper UI Studio
// Icon 12x12 - 1bpp, idx=y*cols+x, byte=idx/8, bit=idx%8, LSB-first
// Install: firmware/assets/icon_games.h, entry goes in PIC_LIBRARY[] (assets.cpp)
#pragma once

#include <Arduino.h>

const uint8_t PIC_GAMES[18] PROGMEM = {
  0x00, 0x00, 0x00, 0xfc, 0x23, 0x40, 0x09, 0xd9, 0xa9, 0x09, 0x19, 0x80, 0xf1, 0xa8, 0x50, 0x04,
  0x02, 0x00,
};

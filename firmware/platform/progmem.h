// progmem.h - reads a byte of asset data. On ESP32 PROGMEM data is memory-mapped, so this is a plain load
// there too; the wrapper only exists so an AVR-style port would have one place to change.
#pragma once

#include <cstdint>

inline uint8_t progmemByte(const uint8_t* p) { return *p; }

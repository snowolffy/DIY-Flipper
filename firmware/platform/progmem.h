// progmem.h - reads asset data. On ESP32 PROGMEM data is memory-mapped, so these are plain loads there too;
// the wrappers only exist so a port that needs special flash reads has one place to change.
#pragma once

#include <cstdint>

inline uint8_t progmemByte(const uint8_t* p) { return *p; }
inline uint16_t progmemWord(const uint16_t* p) { return *p; }

// colors.h - every color the UI uses, in one place (RGB565). The look is grey-scale for now; an accent
// color can be added here later without touching the screens (they all ask for these names).
#pragma once

#include <cstdint>

namespace ui {
namespace color {

constexpr uint16_t kBlack = 0x0000;
constexpr uint16_t kWhite = 0xFFFF;  // selected item, main text
constexpr uint16_t kGray = 0x8410;   // not selected, secondary text
constexpr uint16_t kDark = 0x4A49;   // disabled / faded, weak signal bars
constexpr uint16_t kLight = 0xC618;  // placeholder art (game canvas)
constexpr uint16_t kAccent = kWhite; // no accent yet: keep it white until one is chosen

// The key Flipper UI Studio writes for "not drawn" pixels in pictures and icons.
constexpr uint16_t kTransparent = 0xF81F;

// What a pixel looks like behind a dialog: each channel x 100/256, rounded down (white -> 0x630C,
// grey -> 0x3186, light grey -> 0x4A49), the values the Studio mockups use.
constexpr uint16_t dimmed(uint16_t c) {
  return (uint16_t)(((((c >> 11) & 31) * 100 >> 8) << 11) | ((((c >> 5) & 63) * 100 >> 8) << 5) |
                    ((c & 31) * 100 >> 8));
}

static_assert(dimmed(kWhite) == 0x630C, "dim white");
static_assert(dimmed(kGray) == 0x3186, "dim grey");
static_assert(dimmed(kLight) == 0x4A49, "dim light grey");

}  // namespace color
}  // namespace ui

// assets.h - built-in fonts, icons and pictures. The data lives in generated/*.h (exported from
// Flipper UI Studio) and is included by assets.cpp only, so each table is compiled once.
#pragma once

#include "ui/gfx.h"

namespace assets {

extern const FontEntry kFontLarge;  // 8x8, menus, titles, input
extern const FontEntry kFontSmall;  // 6x8, status bar, labels, values

// status bar, 8 px tall
extern const PicEntry kIconBattery;
extern const PicEntry kIconWifi;
extern const PicEntry kIconBluetooth;

// main menu, 12x12
extern const PicEntry kIconIr;
extern const PicEntry kIconNfc;
extern const PicEntry kIconGames;
extern const PicEntry kIconWifiSetup;
extern const PicEntry kIconBluetoothRemote;
extern const PicEntry kIconSettings;

extern const PicEntry kBootSplash;  // 128x160

// Registry tables, the ones Flipper UI Studio's install steps tell you to add entries to.
extern const PicEntry PIC_LIBRARY[];
extern const int PIC_LIBRARY_COUNT;
extern const FontEntry FONT_LIBRARY[];
extern const int FONT_LIBRARY_COUNT;

}  // namespace assets

// assets.h - built-in fonts, icons and animations. The data lives in generated/*.h (exported from
// Flipper UI Studio) and is included by assets.cpp only, so each table is compiled once.
#pragma once

#include "ui/gfx.h"

namespace assets {

extern const FontEntry kFontLarge;  // 8x8: logo, big numbers
extern const FontEntry kFontSmall;  // 6x8: everything else

// status bar
extern const PicEntry kIconBattery;    // 10x8
extern const PicEntry kIconWifi;       // 8x8
extern const PicEntry kIconBluetooth;  // 8x8
// launcher, 12x12
extern const PicEntry kIconIr;
extern const PicEntry kIconNfc;
extern const PicEntry kIconGames;
extern const PicEntry kIconWifiSetup;
extern const PicEntry kIconBluetoothRemote;
extern const PicEntry kIconSettings;
// toolkit
extern const PicEntry kIconArrow;      // 8x8, end of the selected launcher card
extern const PicEntry kIconPie;        // 24x24, boot logo
extern const PicEntry kIconFolder;     // 10x8
extern const PicEntry kIconPlus;       // 7x7
extern const PicEntry kIconCheck;      // 8x8
extern const PicEntry kIconBackspace;  // 11x9
extern const PicEntry kIconLock;       // 8x8
extern const PicEntry kIconDot;        // 6x6
extern const GifEntry kAnimLoading;    // 12x12, 4 frames
extern const PicEntry kBootLogo;       // 78x40, pie + "Pie Controller", drawn at (26, 56)

// Registry tables, the ones Flipper UI Studio's REGISTRY.txt lists entries for.
extern const PicEntry PIC_LIBRARY[];
extern const int PIC_LIBRARY_COUNT;
extern const GifEntry GIF_LIBRARY[];
extern const int GIF_LIBRARY_COUNT;
extern const FontEntry FONT_LIBRARY[];
extern const int FONT_LIBRARY_COUNT;

}  // namespace assets

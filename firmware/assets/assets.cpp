// assets.cpp - the asset registry. To add an export from Flipper UI Studio: save the .h into generated/,
// #include it below, and add its entry line to PIC_LIBRARY[] or FONT_LIBRARY[].
#include "assets/assets.h"

#include "generated/font_large.h"
#include "generated/font_small.h"
#include "generated/icon_battery.h"
#include "generated/icon_bluetooth.h"
#include "generated/icon_bluetooth_remote.h"
#include "generated/icon_games.h"
#include "generated/icon_ir.h"
#include "generated/icon_nfc.h"
#include "generated/icon_settings.h"
#include "generated/icon_wifi.h"
#include "generated/icon_wifi_setup.h"
#include "generated/pic_boot_splash.h"

namespace assets {

const FontEntry kFontLarge = {"Large", LARGE_GLYPHS, LARGE_CHARSET, LARGE_W, LARGE_H, LARGE_COUNT};
const FontEntry kFontSmall = {"Small", SMALL_GLYPHS, SMALL_CHARSET, SMALL_W, SMALL_H, SMALL_COUNT};

const PicEntry kIconBattery = {"battery", PIC_BATTERY, 10, 8};
const PicEntry kIconWifi = {"wifi", PIC_WIFI, 8, 8};
const PicEntry kIconBluetooth = {"bluetooth", PIC_BLUETOOTH, 8, 8};

const PicEntry kIconIr = {"ir", PIC_IR, 12, 12};
const PicEntry kIconNfc = {"nfc", PIC_NFC, 12, 12};
const PicEntry kIconGames = {"games", PIC_GAMES, 12, 12};
const PicEntry kIconWifiSetup = {"wifi_setup", PIC_WIFI_SETUP, 12, 12};
const PicEntry kIconBluetoothRemote = {"bluetooth_remote", PIC_BLUETOOTH_REMOTE, 12, 12};
const PicEntry kIconSettings = {"settings", PIC_SETTINGS, 12, 12};

const PicEntry kBootSplash = {"Boot splash", PIC_BOOT_SPLASH, 128, 160};

const PicEntry PIC_LIBRARY[] = {
    {"Boot splash", PIC_BOOT_SPLASH, 128, 160},
};
const int PIC_LIBRARY_COUNT = sizeof(PIC_LIBRARY) / sizeof(PIC_LIBRARY[0]);

const FontEntry FONT_LIBRARY[] = {
    {"Large", LARGE_GLYPHS, LARGE_CHARSET, LARGE_W, LARGE_H, LARGE_COUNT},
    {"Small", SMALL_GLYPHS, SMALL_CHARSET, SMALL_W, SMALL_H, SMALL_COUNT},
};
const int FONT_LIBRARY_COUNT = sizeof(FONT_LIBRARY) / sizeof(FONT_LIBRARY[0]);

}  // namespace assets

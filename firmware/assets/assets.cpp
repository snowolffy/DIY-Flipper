// assets.cpp - the asset registry. To add an export from Flipper UI Studio: save the .h into generated/,
// #include it below, and add its entry line (from the export's REGISTRY.txt) to the matching table.
#include "assets/assets.h"

#include "generated/font_small_6x8.h"
#include "generated/font_large_8x8.h"
#include "generated/icon_battery.h"
#include "generated/icon_wifi.h"
#include "generated/icon_bluetooth.h"
#include "generated/icon_ir.h"
#include "generated/icon_nfc.h"
#include "generated/icon_games.h"
#include "generated/icon_wifi_setup.h"
#include "generated/pic_boot_logo.h"
#include "generated/icon_bluetooth_remote.h"
#include "generated/icon_settings.h"
#include "generated/icon_arrow1.h"
#include "generated/icon_pie1.h"
#include "generated/anim_loading_gif1.h"
#include "generated/icon_folder_new.h"
#include "generated/icon_plus_new.h"
#include "generated/icon_check_new.h"
#include "generated/icon_backspace_new.h"
#include "generated/icon_lock_new.h"
#include "generated/icon_status_dot_new.h"

namespace assets {

const PicEntry PIC_LIBRARY[] = {
    {"Battery", PIC_BATTERY, 10, 8},
    {"WiFi", PIC_WIFI, 8, 8},
    {"Bluetooth", PIC_BLUETOOTH, 8, 8},
    {"IR", PIC_IR, 12, 12},
    {"NFC", PIC_NFC, 12, 12},
    {"Games", PIC_GAMES, 12, 12},
    {"WiFi Setup", PIC_WIFI_SETUP, 12, 12},
    {"Bluetooth Remote", PIC_BLUETOOTH_REMOTE, 12, 12},
    {"Settings", PIC_SETTINGS, 12, 12},
    {"arrow1", PIC_ARROW1, 8, 8},
    {"pie1", PIC_PIE1, 24, 24},
    {"folder-new", PIC_FOLDER_NEW, 10, 8},
    {"plus-new", PIC_PLUS_NEW, 7, 7},
    {"check-new", PIC_CHECK_NEW, 8, 8},
    {"backspace-new", PIC_BACKSPACE_NEW, 11, 9},
    {"lock-new", PIC_LOCK_NEW, 8, 8},
    {"status-dot-new", PIC_STATUS_DOT_NEW, 6, 6},
};
const int PIC_LIBRARY_COUNT = sizeof(PIC_LIBRARY) / sizeof(PIC_LIBRARY[0]);

const GifEntry GIF_LIBRARY[] = {
    {"Loading-gif1", LOADING_GIF1_FRAMES, LOADING_GIF1_FRAME_COUNT, LOADING_GIF1_FRAME_DELAY_MS, LOADING_GIF1_W, LOADING_GIF1_H},
};
const int GIF_LIBRARY_COUNT = sizeof(GIF_LIBRARY) / sizeof(GIF_LIBRARY[0]);

const FontEntry FONT_LIBRARY[] = {
    {"Small 6x8", SMALL_6X8_GLYPHS, SMALL_6X8_CHARSET, SMALL_6X8_W, SMALL_6X8_H, SMALL_6X8_COUNT},
    {"Large 8x8", LARGE_8X8_GLYPHS, LARGE_8X8_CHARSET, LARGE_8X8_W, LARGE_8X8_H, LARGE_8X8_COUNT},
};
const int FONT_LIBRARY_COUNT = sizeof(FONT_LIBRARY) / sizeof(FONT_LIBRARY[0]);

const FontEntry kFontSmall = FONT_LIBRARY[0];
const FontEntry kFontLarge = FONT_LIBRARY[1];

const PicEntry kIconBattery = PIC_LIBRARY[0];
const PicEntry kIconWifi = PIC_LIBRARY[1];
const PicEntry kIconBluetooth = PIC_LIBRARY[2];
const PicEntry kIconIr = PIC_LIBRARY[3];
const PicEntry kIconNfc = PIC_LIBRARY[4];
const PicEntry kIconGames = PIC_LIBRARY[5];
const PicEntry kIconWifiSetup = PIC_LIBRARY[6];
const PicEntry kIconBluetoothRemote = PIC_LIBRARY[7];
const PicEntry kIconSettings = PIC_LIBRARY[8];
const PicEntry kIconArrow = PIC_LIBRARY[9];
const PicEntry kIconPie = PIC_LIBRARY[10];
const PicEntry kIconFolder = PIC_LIBRARY[11];
const PicEntry kIconPlus = PIC_LIBRARY[12];
const PicEntry kIconCheck = PIC_LIBRARY[13];
const PicEntry kIconBackspace = PIC_LIBRARY[14];
const PicEntry kIconLock = PIC_LIBRARY[15];
const PicEntry kIconDot = PIC_LIBRARY[16];
const GifEntry kAnimLoading = GIF_LIBRARY[0];
// not in the Studio export: cut from the "Boot Load" mockup (tools/png_to_pic.js)
const PicEntry kBootLogo = {"boot_logo", PIC_BOOT_LOGO, 78, 40};

}  // namespace assets

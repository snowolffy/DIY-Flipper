#include "app/theme.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>

#include "assets/assets.h"

namespace theme {

namespace {

struct Slot {
  const char* key;
  const PicEntry* builtIn;
  uint16_t w, h;
};

// Every icon slot the UI draws, with the size its layout reserves.
const Slot kIconSlots[] = {
    {"battery", &assets::kIconBattery, 10, 8},
    {"wifi", &assets::kIconWifi, 8, 8},
    {"bluetooth", &assets::kIconBluetooth, 8, 8},
    {"ir", &assets::kIconIr, 12, 12},
    {"nfc", &assets::kIconNfc, 12, 12},
    {"games", &assets::kIconGames, 12, 12},
    {"wifi_setup", &assets::kIconWifiSetup, 12, 12},
    {"bluetooth_remote", &assets::kIconBluetoothRemote, 12, 12},
    {"settings", &assets::kIconSettings, 12, 12},
    {"arrow1", &assets::kIconArrow, 8, 8},
    {"pie1", &assets::kIconPie, 24, 24},
    {"folder_new", &assets::kIconFolder, 10, 8},
    {"plus_new", &assets::kIconPlus, 7, 7},
    {"check_new", &assets::kIconCheck, 8, 8},
    {"backspace_new", &assets::kIconBackspace, 11, 9},
    {"lock_new", &assets::kIconLock, 8, 8},
    {"status_dot_new", &assets::kIconDot, 6, 6},
};

const PicEntry kEmpty = {"", nullptr, 0, 0};

// A loaded theme owns its pixel data; the Entry structs point into it.
struct Loaded {
  std::string name;
  Font large, small;
  bool hasLarge = false, hasSmall = false;
  FontEntry largeE{}, smallE{};
  Image splash;
  bool hasSplash = false;
  PicEntry splashE{};
  Image wallpaper;
  bool hasWallpaper = false;
  PicEntry wallpaperE{};
  Image loading;
  bool hasLoading = false;
  std::vector<const uint16_t*> loadingFrames;
  GifEntry loadingE{};
  std::map<std::string, Image> icons;
  std::map<std::string, PicEntry> iconE;
};

std::unique_ptr<Loaded> g;
const std::string kNone;

uint16_t u16(const std::string& b, size_t at) {
  return (uint16_t)((uint8_t)b[at] | ((uint8_t)b[at + 1] << 8));
}

std::string trim(const std::string& s) {
  size_t b = 0, e = s.size();
  while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r')) b++;
  while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) e--;
  return s.substr(b, e - b);
}

// [section] key=value, comments start with ';' or '#'. Keys come back as "section.key".
std::map<std::string, std::string> parseIni(const std::string& text) {
  std::map<std::string, std::string> out;
  std::string section;
  size_t pos = 0;
  while (pos <= text.size()) {
    size_t end = text.find('\n', pos);
    if (end == std::string::npos) end = text.size();
    const std::string line = trim(text.substr(pos, end - pos));
    pos = end + 1;
    if (line.empty() || line[0] == ';' || line[0] == '#') continue;
    if (line.front() == '[' && line.back() == ']') {
      section = trim(line.substr(1, line.size() - 2));
      continue;
    }
    const size_t eq = line.find('=');
    if (eq != std::string::npos) out[section + "." + trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
  }
  return out;
}

bool safeRelative(const std::string& p) {
  return !p.empty() && p[0] != '/' && p.find("..") == std::string::npos && p.find('\\') == std::string::npos;
}

}  // namespace

bool parseC16(const std::string& b, Image& out, std::string& err) {
  if (b.size() < 14 || b.compare(0, 3, "C16") != 0) {
    err = "not a .c16 image";
    return false;
  }
  if ((uint8_t)b[3] != 1) {
    err = "unsupported .c16 version " + std::to_string((uint8_t)b[3]);
    return false;
  }
  out.w = u16(b, 4);
  out.h = u16(b, 6);
  out.frames = u16(b, 8);
  out.delayMs = u16(b, 10);
  const uint16_t key = u16(b, 12);
  const size_t n = (size_t)out.w * out.h * out.frames;
  if (out.w == 0 || out.h == 0 || out.frames == 0 || b.size() < 14 + n * 2) {
    err = "truncated .c16 image";
    return false;
  }
  out.px.resize(n);
  for (size_t i = 0; i < n; i++) {
    uint16_t c = u16(b, 14 + i * 2);
    // the file's own key marks see-through pixels; a real pixel of the in-memory key colour is nudged
    // off it (the Studio does the same at export) so it still shows
    if (c == key) c = ui::color::kTransparent;
    else if (c == ui::color::kTransparent) c = 0xF83F;
    out.px[i] = c;
  }
  return true;
}

bool parseB1i(const std::string& b, Image& out, std::string& err) {
  if (b.size() < 12 || b.compare(0, 3, "B1I") != 0) {
    err = "not a .b1i image";
    return false;
  }
  if ((uint8_t)b[3] != 1) {
    err = "unsupported .b1i version " + std::to_string((uint8_t)b[3]);
    return false;
  }
  out.w = u16(b, 4);
  out.h = u16(b, 6);
  out.frames = u16(b, 8);
  out.delayMs = u16(b, 10);
  const size_t pixels = (size_t)out.w * out.h;
  const size_t frameBytes = (pixels + 7) / 8;
  if (out.w == 0 || out.h == 0 || out.frames == 0 || b.size() < 12 + frameBytes * out.frames) {
    err = "truncated .b1i image";
    return false;
  }
  out.px.resize(pixels * out.frames);
  for (size_t f = 0; f < out.frames; f++)
    for (size_t i = 0; i < pixels; i++) {
      const bool ink = ((uint8_t)b[12 + f * frameBytes + (i >> 3)] >> (i & 7)) & 1;
      out.px[f * pixels + i] = ink ? ui::color::kWhite : ui::color::kTransparent;
    }
  return true;
}

bool parseImage(const std::string& b, Image& out, std::string& err) {
  if (b.compare(0, 3, "C16") == 0) return parseC16(b, out, err);
  if (b.compare(0, 3, "B1I") == 0) return parseB1i(b, out, err);
  err = "not a .c16 or .b1i image";
  return false;
}

bool parseB1f(const std::string& b, Font& out, std::string& err) {
  if (b.size() < 8 || b.compare(0, 3, "B1F") != 0) {
    err = "not a .b1f font";
    return false;
  }
  if ((uint8_t)b[3] != 1) {
    err = "unsupported .b1f version " + std::to_string((uint8_t)b[3]);
    return false;
  }
  out.w = (uint8_t)b[4];
  out.h = (uint8_t)b[5];
  const uint16_t n = u16(b, 6);
  const size_t glyphBytes = ((size_t)out.w * out.h + 7) / 8;
  if (out.w == 0 || out.h == 0 || n == 0 || b.size() < 8 + n + glyphBytes * n) {
    err = "truncated .b1f font";
    return false;
  }
  out.charset = b.substr(8, n);
  if (out.charset.find('\0') != std::string::npos) {
    err = ".b1f charset contains a zero byte";
    return false;
  }
  out.glyphs.assign(b.begin() + 8 + n, b.begin() + 8 + n + (std::ptrdiff_t)(glyphBytes * n));
  return true;
}

const FontEntry& large() { return g && g->hasLarge ? g->largeE : assets::kFontLarge; }
const FontEntry& small() { return g && g->hasSmall ? g->smallE : assets::kFontSmall; }
const PicEntry* splash() { return g && g->hasSplash ? &g->splashE : nullptr; }
const PicEntry* wallpaper() { return g && g->hasWallpaper ? &g->wallpaperE : nullptr; }
const GifEntry& loading() { return g && g->hasLoading ? g->loadingE : assets::kAnimLoading; }

const PicEntry& icon(const char* key) {
  if (g) {
    auto it = g->iconE.find(key);
    if (it != g->iconE.end()) return it->second;
  }
  for (const Slot& s : kIconSlots)
    if (std::strcmp(s.key, key) == 0) return *s.builtIn;
  return kEmpty;
}

const std::string& activeName() { return g ? g->name : kNone; }

void useBuiltIn() { g.reset(); }

std::vector<std::string> available(const hal::Storage& storage) {
  std::vector<std::string> dirs;
  storage.listDirs(hal::Volume::Sd, kRoot, dirs);
  return dirs;
}

namespace {

std::unique_ptr<Loaded> read(const hal::Storage& storage, const std::string& name, std::string& err,
                             std::vector<std::string>& warnings) {
  warnings.clear();
  if (!safeRelative(name) || name.find('/') != std::string::npos) {
    err = "bad theme name";
    return nullptr;
  }
  const std::string dir = std::string(kRoot) + "/" + name;
  std::string iniText;
  if (!storage.read(hal::Volume::Sd, dir + "/theme.ini", iniText)) {
    err = "no theme.ini in " + dir;
    return nullptr;
  }
  const auto ini = parseIni(iniText);
  auto t = std::make_unique<Loaded>();
  t->name = name;

  auto readFile = [&](const std::string& rel, std::string& bytes) {
    if (!safeRelative(rel)) {
      warnings.push_back(rel + ": path must stay inside the theme folder");
      return false;
    }
    if (!storage.read(hal::Volume::Sd, dir + "/" + rel, bytes)) {
      warnings.push_back(rel + ": missing");
      return false;
    }
    return true;
  };
  auto loadFont = [&](const char* key, uint8_t w, uint8_t h, Font& out, bool& has, FontEntry& e) {
    auto it = ini.find(std::string("theme.") + key);
    if (it == ini.end()) return;
    std::string bytes, e2;
    if (!readFile(it->second, bytes)) return;
    if (!parseB1f(bytes, out, e2)) {
      warnings.push_back(it->second + ": " + e2);
      return;
    }
    if (out.w != w || out.h != h) {
      warnings.push_back(it->second + ": font is " + std::to_string(out.w) + "x" + std::to_string(out.h) + ", needs " +
                         std::to_string(w) + "x" + std::to_string(h));
      return;
    }
    has = true;
    e = {key, out.glyphs.data(), out.charset.c_str(), out.w, out.h, (uint16_t)out.charset.size()};
  };
  loadFont("font_large", 8, 8, t->large, t->hasLarge, t->largeE);
  loadFont("font_small", 6, 8, t->small, t->hasSmall, t->smallE);

  auto loadImage = [&](const std::string& rel, uint16_t w, uint16_t h, Image& out) {
    std::string bytes, e2;
    if (!readFile(rel, bytes)) return false;
    if (!parseImage(bytes, out, e2)) {
      warnings.push_back(rel + ": " + e2);
      return false;
    }
    if (out.w != w || out.h != h) {
      warnings.push_back(rel + ": image is " + std::to_string(out.w) + "x" + std::to_string(out.h) + ", needs " +
                         std::to_string(w) + "x" + std::to_string(h));
      return false;
    }
    return true;
  };
  auto splashIt = ini.find("theme.splash");
  if (splashIt != ini.end() && loadImage(splashIt->second, 128, 160, t->splash)) {
    t->hasSplash = true;
    t->splashE = {"splash", t->splash.px.data(), t->splash.w, t->splash.h};
  }
  auto wallIt = ini.find("theme.wallpaper");
  if (wallIt != ini.end() && loadImage(wallIt->second, 128, 137, t->wallpaper)) {
    t->hasWallpaper = true;
    t->wallpaperE = {"wallpaper", t->wallpaper.px.data(), t->wallpaper.w, t->wallpaper.h};
  }
  for (const auto& kv : ini) {
    if (kv.first.rfind("icons.", 0) != 0) continue;
    const std::string key = kv.first.substr(6);
    if (key == "loading_gif1") {
      if (loadImage(kv.second, 12, 12, t->loading)) {
        t->hasLoading = true;
        const size_t n = (size_t)t->loading.w * t->loading.h;
        for (uint16_t f = 0; f < t->loading.frames; f++) t->loadingFrames.push_back(t->loading.px.data() + f * n);
        t->loadingE = {"loading", t->loadingFrames.data(), (int)t->loading.frames,
                       (unsigned long)(t->loading.delayMs ? t->loading.delayMs : 150), 12, 12};
      }
      continue;
    }
    const Slot* slot = nullptr;
    for (const Slot& s : kIconSlots)
      if (key == s.key) slot = &s;
    if (!slot) {
      warnings.push_back("icon '" + key + "' isn't used by this firmware");
      continue;
    }
    Image img;
    if (!loadImage(kv.second, slot->w, slot->h, img)) continue;
    t->icons[key] = std::move(img);
  }
  // pointers into the map (key strings and pixel vectors) stay valid for as long as the theme is loaded
  for (auto& kv : t->icons) t->iconE[kv.first] = {kv.first.c_str(), kv.second.px.data(), kv.second.w, kv.second.h};
  return t;
}

}  // namespace

bool load(const hal::Storage& storage, const std::string& name, std::string& err, std::vector<std::string>& warnings) {
  auto t = read(storage, name, err, warnings);
  if (!t) return false;
  g = std::move(t);
  return true;
}

bool validate(const hal::Storage& storage, const std::string& name, std::string& err,
              std::vector<std::string>& warnings) {
  return read(storage, name, err, warnings) != nullptr;
}

}  // namespace theme

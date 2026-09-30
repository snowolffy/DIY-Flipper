// radio_apps.cpp - the IR, NFC, WiFi Setup and Bluetooth Remote apps, built on the List / Detail /
// Text-input templates plus a few screens of their own.
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "app/minijson.h"
#include "app/screens.h"
#include "assets/assets.h"

namespace app {

using ui::Framebuffer;

namespace {

bool isShort(const ButtonEvent& e, hal::Button b) { return e.button == b && e.press == Press::Short; }

std::string hex(uint32_t v, int digits) {
  char b[16];
  std::snprintf(b, sizeof(b), "0x%0*X", digits, (unsigned)v);
  return b;
}

uint32_t parseNumber(const std::string& s) { return (uint32_t)std::strtoul(s.c_str(), nullptr, 0); }

std::string stripExt(const std::string& name) {
  const size_t dot = name.rfind('.');
  return dot == std::string::npos ? name : name.substr(0, dot);
}

// Title row + wrapped small-font text, the layout every "waiting for something" screen uses.
int16_t drawMessage(Framebuffer& fb, const char* title, const char* body) {
  using namespace ui;
  drawText(fb, assets::kFontLarge, kPad, kStatusH + 2, title);
  fb.hline(0, kStatusH + kTitleH - 1, kScreenW, true);
  return drawWrapped(fb, assets::kFontSmall, kPad, kStatusH + kTitleH + 3, kScreenW - kPad * 2, kDetailRowH, body);
}

// Label/value rows in the Detail layout, starting at y. Returns the y below the last row.
int16_t drawRows(Framebuffer& fb, int16_t y, const DetailScreen::Rows& rows) {
  using namespace ui;
  for (const auto& kv : rows) {
    drawText(fb, assets::kFontSmall, kPad, y, kv.first.c_str());
    drawTextRight(fb, assets::kFontSmall, kScreenW - kPad, y, kv.second.c_str());
    y += kDetailRowH;
  }
  return y;
}

DetailScreen::Rows signalRows(const hal::IrSignal& s) {
  if (s.protocol == "RAW") return {{"Protocol", "RAW"}, {"Timings", std::to_string(s.raw.size())}};
  return {{"Protocol", s.protocol}, {"Address", hex(s.address, 2)}, {"Command", hex(s.command, 2)}};
}

// ============================ IR ============================

constexpr const char* kIrDir = "/ir/uncategorized";

std::string irToJson(const std::string& name, const hal::IrSignal& s) {
  std::string j = "{\n  \"name\": \"" + minijson::escape(name) + "\",\n  \"protocol\": \"" + s.protocol + "\"";
  if (s.protocol == "RAW") {
    j += ",\n  \"raw\": [";
    for (size_t i = 0; i < s.raw.size(); i++) j += (i ? ", \"" : "\"") + std::to_string(s.raw[i]) + "\"";
    j += "]";
  } else {
    j += ",\n  \"address\": \"" + hex(s.address, 2) + "\",\n  \"command\": \"" + hex(s.command, 2) + "\"";
  }
  return j + "\n}\n";
}

bool irFromJson(const std::string& json, hal::IrSignal& s) {
  s.protocol = minijson::field(json, "protocol");
  if (s.protocol.empty()) return false;
  s.address = parseNumber(minijson::field(json, "address"));
  s.command = parseNumber(minijson::field(json, "command"));
  s.raw.clear();
  for (const std::string& v : minijson::stringArray(json, "raw")) s.raw.push_back((uint16_t)parseNumber(v));
  return true;
}

class IrLearnScreen : public Screen {
 public:
  const char* title() const override { return "Learn"; }
  bool hasTitleRow() const override { return true; }
  void onEnter(App& app) override { app.hal().ir.setListening(true); }

  void onTick(App& app) override {
    hal::IrSignal s;
    if (!got_ && app.hal().ir.receive(s)) {
      signal_ = s;
      got_ = true;
      app.hal().ir.setListening(false);
    }
  }

  void onEvent(App& app, const ButtonEvent& e) override {
    if (isShort(e, hal::Button::Cancel)) {
      if (got_) {  // try again
        got_ = false;
        error_.clear();
        app.hal().ir.setListening(true);
      } else {
        app.hal().ir.setListening(false);
        app.pop();
      }
    } else if (isShort(e, hal::Button::Ok) && got_) {
      save(app);
    }
  }

  void draw(App&, Framebuffer& fb) override {
    if (!got_) {
      drawMessage(fb, "Learn", "Point the remote at the IR eye and press the button to copy.");
      return;
    }
    int16_t y = drawMessage(fb, "Learn", "Got it.");
    y = drawRows(fb, y + 2, signalRows(signal_));
    drawWrapped(fb, assets::kFontSmall, ui::kPad, y + 6, ui::kScreenW - ui::kPad * 2, ui::kDetailRowH,
                error_.empty() ? "OK save   Back retry" : error_.c_str());
  }

 private:
  void save(App& app) {
    hal::Storage& st = app.hal().storage;
    if (!st.present(hal::Volume::Sd)) {
      error_ = "No SD card. Insert one and press OK again.";
      return;
    }
    std::string name = "new_remote";
    for (int n = 2; st.exists(hal::Volume::Sd, std::string(kIrDir) + "/" + name + ".json"); n++)
      name = "new_remote_" + std::to_string(n);
    const std::string path = std::string(kIrDir) + "/" + name + ".json";
    if (!st.write(hal::Volume::Sd, path, irToJson(name, signal_))) {
      error_ = "Couldn't write to the SD card.";
      return;
    }
    app.replaceTop(std::make_unique<DetailScreen>("Saved", [name](App&) {
      return DetailScreen::Rows{{"Name", name}, {"Folder", "uncategorized"}};
    }));
  }

  hal::IrSignal signal_;
  bool got_ = false;
  std::string error_;
};

// A saved remote: OK transmits it.
class IrRemoteScreen : public Screen {
 public:
  IrRemoteScreen(std::string name, hal::IrSignal s) : name_(std::move(name)), signal_(std::move(s)) {}
  const char* title() const override { return name_.c_str(); }
  bool hasTitleRow() const override { return true; }

  void onEvent(App& app, const ButtonEvent& e) override {
    if (isShort(e, hal::Button::Cancel)) app.pop();
    else if (isShort(e, hal::Button::Ok) && app.hal().ir.send(signal_)) sent_++;
  }

  void draw(App&, Framebuffer& fb) override {
    int16_t y = drawMessage(fb, name_.c_str(), "");
    y = drawRows(fb, y, signalRows(signal_));
    DetailScreen::Rows status = {{"Sent", std::to_string(sent_) + (sent_ == 1 ? " time" : " times")}};
    y = drawRows(fb, y + 4, status);
    ui::drawText(fb, assets::kFontSmall, ui::kPad, y + 6, "OK SEND   BACK");
  }

 private:
  std::string name_;
  hal::IrSignal signal_;
  int sent_ = 0;
};

// ============================ NFC ============================

constexpr const char* kNfcDir = "/nfc";

std::string nfcToJson(const hal::NfcCard& c) {
  std::string j = "{\n  \"uid\": \"" + c.uid + "\",\n  \"type\": \"" + minijson::escape(c.type) + "\",\n  \"blocks\": [";
  for (size_t i = 0; i < c.blocks.size(); i++) j += std::string(i ? ",\n    \"" : "\n    \"") + c.blocks[i] + "\"";
  return j + (c.blocks.empty() ? "]" : "\n  ]") + "\n}\n";
}

class NfcReadScreen : public Screen {
 public:
  const char* title() const override { return "Read card"; }
  bool hasTitleRow() const override { return true; }
  void onEnter(App& app) override { app.hal().nfc.setPolling(true); }

  void onTick(App& app) override {
    hal::NfcCard c;
    if (!got_ && app.hal().nfc.card(c)) {
      card_ = c;
      got_ = true;
      app.hal().nfc.setPolling(false);
    }
  }

  void onEvent(App& app, const ButtonEvent& e) override {
    if (isShort(e, hal::Button::Cancel)) {
      if (got_) {
        got_ = false;
        error_.clear();
        app.hal().nfc.setPolling(true);
      } else {
        app.hal().nfc.setPolling(false);
        app.pop();
      }
    } else if (isShort(e, hal::Button::Ok) && got_) {
      save(app);
    }
  }

  void draw(App&, Framebuffer& fb) override {
    if (!got_) {
      drawMessage(fb, "Read card", "Hold a card or tag flat against the back of the device.");
      return;
    }
    int16_t y = drawMessage(fb, "Read card", "");
    y = drawRows(fb, y, {{"Type", card_.type}, {"Blocks", std::to_string(card_.blocks.size())}});
    ui::drawText(fb, assets::kFontSmall, ui::kPad, y, "UID");
    ui::drawTextRight(fb, assets::kFontSmall, ui::kScreenW - ui::kPad, y + ui::kDetailRowH, card_.uid.c_str());
    drawWrapped(fb, assets::kFontSmall, ui::kPad, y + 2 * ui::kDetailRowH + 6, ui::kScreenW - ui::kPad * 2,
                ui::kDetailRowH, error_.empty() ? "OK save   Back retry" : error_.c_str());
  }

 private:
  void save(App& app) {
    hal::Storage& st = app.hal().storage;
    if (!st.present(hal::Volume::Sd)) {
      error_ = "No SD card. Insert one and press OK again.";
      return;
    }
    const std::string path = std::string(kNfcDir) + "/" + card_.uid + ".json";
    if (!st.write(hal::Volume::Sd, path, nfcToJson(card_))) {
      error_ = "Couldn't write to the SD card.";
      return;
    }
    const hal::NfcCard c = card_;
    app.replaceTop(std::make_unique<DetailScreen>("Saved", [c](App&) {
      return DetailScreen::Rows{{"Type", c.type}, {"UID", c.uid}};
    }));
  }

  hal::NfcCard card_;
  bool got_ = false;
  std::string error_;
};

// ============================ WiFi ============================

std::string bars(int rssi) {
  // 4 steps, shown as the small font's characters so it lines up with the other values
  const int n = rssi >= -55 ? 4 : rssi >= -67 ? 3 : rssi >= -78 ? 2 : 1;
  return std::string((size_t)n, '|') + std::string((size_t)(4 - n), '.');
}

class WifiConnectScreen : public Screen {
 public:
  WifiConnectScreen(std::string ssid, std::string password) : ssid_(std::move(ssid)), password_(std::move(password)) {}
  const char* title() const override { return "Connect"; }
  bool hasTitleRow() const override { return true; }
  void onEnter(App& app) override { app.hal().wifi.connect(ssid_, password_); }

  void onEvent(App& app, const ButtonEvent& e) override {
    const hal::WifiState st = app.hal().wifi.state();
    if (st == hal::WifiState::Connecting) return;  // wait for the answer
    if (isShort(e, hal::Button::Ok) || isShort(e, hal::Button::Cancel)) app.pop();
  }

  void draw(App& app, Framebuffer& fb) override {
    switch (app.hal().wifi.state()) {
      case hal::WifiState::Connecting:
        drawMessage(fb, "Connect", ("Connecting to " + ssid_ + "...").c_str());
        break;
      case hal::WifiState::Connected: {
        const int16_t y = drawMessage(fb, "Connected", "");
        drawRows(fb, y, {{"Network", ssid_}});
        break;
      }
      default:
        drawMessage(fb, "Failed", ("Couldn't join " + ssid_ + ". Check the password and try again.").c_str());
        break;
    }
  }

 private:
  std::string ssid_, password_;
};

class WifiScanScreen : public Screen {
 public:
  const char* title() const override { return "Scan"; }
  bool hasTitleRow() const override { return true; }
  void onEnter(App& app) override { app.hal().wifi.startScan(); }

  void onTick(App& app) override {
    if (done_ || app.hal().wifi.state() == hal::WifiState::Scanning) return;
    done_ = true;
    std::vector<ListScreen::Item> items;
    for (const hal::WifiNetwork& n : app.hal().wifi.scanResults()) {
      const std::string ssid = n.ssid;
      const bool secured = n.secured;
      items.push_back({ssid, nullptr,
                       [ssid, secured](App& a) {
                         if (!secured) {
                           a.push(std::make_unique<WifiConnectScreen>(ssid, ""));
                           return;
                         }
                         a.push(std::make_unique<TextInputScreen>(
                             "Password", "FOR " + ssid, [ssid](App& b, const std::string& pw) {
                               b.replaceTop(std::make_unique<WifiConnectScreen>(ssid, pw));
                             }));
                       },
                       [n](App&) { return (n.secured ? "* " : "") + bars(n.rssi); }});
    }
    if (items.empty()) {
      app.replaceTop(std::make_unique<DetailScreen>("Scan", [](App&) {
        return DetailScreen::Rows{{"Networks", "None found"}};
      }));
    } else {
      app.replaceTop(std::make_unique<ListScreen>("Networks", std::move(items)));
    }
  }

  void onEvent(App& app, const ButtonEvent& e) override {
    if (isShort(e, hal::Button::Cancel)) app.pop();
  }

  void draw(App&, Framebuffer& fb) override { drawMessage(fb, "Scan", "Looking for networks..."); }

 private:
  bool done_ = false;
};

// ============================ Bluetooth Remote ============================

constexpr uint16_t kPlayPause = 0xCD, kNext = 0xB5, kPrev = 0xB6, kVolUp = 0xE9, kVolDown = 0xEA;

class BluetoothRemoteScreen : public Screen {
 public:
  const char* title() const override { return "BT Remote"; }
  bool hasTitleRow() const override { return true; }
  void onEnter(App& app) override { app.hal().ble.startAdvertising("DIY Flipper"); }

  void onEvent(App& app, const ButtonEvent& e) override {
    hal::Ble& ble = app.hal().ble;
    const hal::BleState st = ble.state();
    if (st == hal::BleState::PairingRequest) {
      if (isShort(e, hal::Button::Ok)) ble.confirmPairing(true);
      else if (isShort(e, hal::Button::Cancel)) ble.confirmPairing(false);
      return;
    }
    if (isShort(e, hal::Button::Cancel)) {
      ble.stop();
      app.pop();
      return;
    }
    if (st != hal::BleState::Connected) return;
    if (isShort(e, hal::Button::Ok)) ble.sendKey(kPlayPause);
    else if (isShort(e, hal::Button::Left)) ble.sendKey(kPrev);
    else if (isShort(e, hal::Button::Right)) ble.sendKey(kNext);
    else if (e.button == hal::Button::Left && e.press != Press::Short) ble.sendKey(kVolDown);
    else if (e.button == hal::Button::Right && e.press != Press::Short) ble.sendKey(kVolUp);
  }

  void draw(App& app, Framebuffer& fb) override {
    const hal::Ble& ble = app.hal().ble;
    switch (ble.state()) {
      case hal::BleState::PairingRequest: {
        const int16_t y =
            drawMessage(fb, "Pair?", ("Pair with " + ble.hostName() + "? Check the code matches.").c_str());
        const unsigned pk = (unsigned)(ble.passkey() % 1000000u);
        char code[16];
        std::snprintf(code, sizeof(code), "%03u %03u", pk / 1000, pk % 1000);
        ui::drawText(fb, assets::kFontLarge, (ui::kScreenW - ui::textWidth(assets::kFontLarge, code)) / 2, y + 6, code);
        ui::drawText(fb, assets::kFontSmall, ui::kPad, y + 24, "OK PAIR   BACK REJECT");
        break;
      }
      case hal::BleState::Connected: {
        int16_t y = drawMessage(fb, "Connected", "");
        y = drawRows(fb, y, {{"Host", ble.hostName()}});
        y += 6;
        ui::drawText(fb, assets::kFontSmall, ui::kPad, y, "<  PREVIOUS");
        ui::drawText(fb, assets::kFontSmall, ui::kPad, y + 10, "OK PLAY / PAUSE");
        ui::drawText(fb, assets::kFontSmall, ui::kPad, y + 20, ">  NEXT");
        ui::drawText(fb, assets::kFontSmall, ui::kPad, y + 30, "HOLD < >  VOLUME");
        ui::drawText(fb, assets::kFontSmall, ui::kPad, y + 46, "BACK  DISCONNECT");
        break;
      }
      case hal::BleState::BondListFull:
        drawMessage(fb, "Bond full",
                    ("Can't pair with " + ble.hostName() + ": four devices are already paired. Remove one first.").c_str());
        break;
      default:
        drawMessage(fb, "Waiting", "Pair from your phone or PC. Look for DIY Flipper in its Bluetooth settings.");
        break;
    }
  }
};

}  // namespace

// ============================ menus ============================

std::unique_ptr<Screen> makeIrMenu() {
  return std::make_unique<ListScreen>("IR", [](App& app) {
    std::vector<ListScreen::Item> items = {
        {"Learn new", nullptr, [](App& a) { a.push(std::make_unique<IrLearnScreen>()); }, nullptr}};
    std::vector<std::string> files;
    app.hal().storage.list(hal::Volume::Sd, kIrDir, files);
    for (const std::string& f : files) {
      const std::string name = stripExt(f);
      const std::string path = std::string(kIrDir) + "/" + f;
      items.push_back({name, nullptr,
                       [name, path](App& a) {
                         std::string json;
                         hal::IrSignal s;
                         if (a.hal().storage.read(hal::Volume::Sd, path, json) && irFromJson(json, s))
                           a.push(std::make_unique<IrRemoteScreen>(name, s));
                         else
                           a.push(std::make_unique<DetailScreen>(name, [](App&) {
                             return DetailScreen::Rows{{"File", "Unreadable"}};
                           }));
                       },
                       nullptr});
    }
    return items;
  });
}

std::unique_ptr<Screen> makeNfcMenu() {
  return std::make_unique<ListScreen>("NFC", [](App& app) {
    std::vector<ListScreen::Item> items = {
        {"Read card", nullptr, [](App& a) { a.push(std::make_unique<NfcReadScreen>()); }, nullptr}};
    std::vector<std::string> files;
    app.hal().storage.list(hal::Volume::Sd, kNfcDir, files);
    for (const std::string& f : files) {
      const std::string uid = stripExt(f);
      const std::string path = std::string(kNfcDir) + "/" + f;
      items.push_back({uid, nullptr,
                       [uid, path](App& a) {
                         a.push(std::make_unique<DetailScreen>("Dump", [uid, path](App& b) {
                           std::string json;
                           if (!b.hal().storage.read(hal::Volume::Sd, path, json))
                             return DetailScreen::Rows{{"File", "Unreadable"}};
                           return DetailScreen::Rows{
                               {"Type", minijson::field(json, "type")},
                               {"UID", uid},
                               {"Blocks", std::to_string(minijson::stringArray(json, "blocks").size())}};
                         }));
                       },
                       nullptr});
    }
    return items;
  });
}

std::unique_ptr<Screen> makeWifiMenu() {
  return std::make_unique<ListScreen>("WiFi Setup", [](App& app) {
    std::vector<ListScreen::Item> items = {
        {"Scan networks", nullptr, [](App& a) { a.push(std::make_unique<WifiScanScreen>()); }, nullptr}};
    if (app.hal().wifi.state() == hal::WifiState::Connected) {
      items.push_back({"Disconnect", nullptr,
                       [](App& a) {
                         a.hal().wifi.disconnect();
                         a.push(std::make_unique<DetailScreen>("WiFi", [](App&) {
                           return DetailScreen::Rows{{"Status", "Off"}};
                         }));
                       },
                       [](App& a) { return a.hal().wifi.connectedSsid(); }});
    }
    return items;
  });
}

std::unique_ptr<Screen> makeBluetoothRemote() { return std::make_unique<BluetoothRemoteScreen>(); }

}  // namespace app

// bt.cpp - the Bluetooth-new flow: B-M0 / B-M1 menu (waiting / connected), B-P pair request, B0 remote
// groups, B1 media keys, B1p presentation keys, Keys group (same layout as Media; no node of its own in the
// flow, code "B1k"), B1x not connected, P1 paired devices, P2 detail, P1d forget.
// BLE never starts at boot; it advertises while the Bluetooth menu is open and stops when the menu is left
// with nothing connected. A connection stays when leaving.
#include <cstdio>

#include "app/modules.h"
#include "app/widgets.h"

namespace app {
namespace bt {

using hal::Button;
using hal::HidKey;

namespace {

constexpr const char* kDeviceName = "Pie Controller";
constexpr const char* kSeenPath = "/ble_seen.ini";  // host<TAB>HH:MM it last connected

bool connected(App& app) { return app.hal().ble.state() == hal::BleState::Connected; }

void recordSeen(App& app) {
  const std::string host = app.hal().ble.hostName();
  hal::DateTime t;
  if (host.empty() || !app.localTime(t)) return;
  std::string text, out;
  app.hal().storage.read(hal::Volume::Flash, kSeenPath, text);
  size_t p = 0;
  while (p < text.size()) {
    size_t e = text.find('\n', p);
    if (e == std::string::npos) e = text.size();
    const std::string line = text.substr(p, e - p);
    if (line.compare(0, host.size() + 1, host + "\t") != 0 && !line.empty()) out += line + "\n";
    p = e + 1;
  }
  char hm[8];
  std::snprintf(hm, sizeof(hm), "%02u:%02u", (unsigned)t.hour, (unsigned)t.minute);
  out += host + "\t" + hm + "\n";
  app.hal().storage.write(hal::Volume::Flash, kSeenPath, out);
}

std::string lastSeen(App& app, const std::string& host) {
  std::string text;
  if (!app.hal().storage.read(hal::Volume::Flash, kSeenPath, text)) return "-";
  const size_t p = text.find(host + "\t");
  if (p == std::string::npos) return "-";
  const size_t e = text.find('\n', p);
  return text.substr(p + host.size() + 1, (e == std::string::npos ? text.size() : e) - p - host.size() - 1);
}

// ---- key pages ----
struct Key {
  const char* label;
  HidKey key;
};

class KeysScreen : public ListScreen {
 public:
  KeysScreen(const char* code, const char* title, std::vector<Key> keys)
      : ListScreen(code, title, title), keys_(std::move(keys)), code0_(code) {}
  // B1x while the host is gone (the flow's "Not connected" state)
  const char* code() const override { return down_ ? "B1x" : code0_; }
  uint8_t deferMask() const override { return buttonBit(Button::Ok) | buttonBit(Button::Cancel); }
  void onEnter(App& app) override {
    down_ = !connected(app);
    ListScreen::onEnter(app);
  }
  void onSystem(App& app, SysEvent e) override {
    if (e == SysEvent::BleDisconnected) down_ = true;
    if (e == SysEvent::BleConnected) down_ = false;
    reload(app);
  }
  void onInput(App& app, const InputEvent& e) override {
    if (!down_ && (e.tap(Button::Ok) || (e.button == Button::Ok && e.gesture == Gesture::Repeat && e.held < 0))) {
      if (!app.hal().ble.sendKey(keys_[sel_].key)) down_ = true;
      return;
    }
    if (e.tap(Button::Ok)) return;
    ListScreen::onInput(app, e);
  }
  void draw(App& app, ui::Framebuffer& fb) override {
    ListScreen::draw(app, fb);
    if (down_) tk::toast(fb, "NOT CONNECTED");
  }

 protected:
  std::vector<tk::Row> rows(App&) override {
    std::vector<tk::Row> r;
    for (const auto& k : keys_) {
      tk::Row row;
      row.label = k.label;
      row.disabled = down_;
      r.push_back(row);
    }
    return r;
  }

 private:
  std::vector<Key> keys_;
  const char* code0_;
  bool down_ = false;
};

std::unique_ptr<Screen> makeMedia() {
  return std::make_unique<KeysScreen>("B1", "MEDIA", std::vector<Key>{
      {"PLAY/PAUSE", {HidKey::Consumer, 0xCD}}, {"NEXT", {HidKey::Consumer, 0xB5}}, {"PREV", {HidKey::Consumer, 0xB6}},
      {"VOL +", {HidKey::Consumer, 0xE9}}, {"VOL -", {HidKey::Consumer, 0xEA}}, {"MUTE", {HidKey::Consumer, 0xE2}}});
}
std::unique_ptr<Screen> makePresentation() {
  return std::make_unique<KeysScreen>("B1p", "PRESENTATION", std::vector<Key>{
      {"NEXT SLIDE", {HidKey::Keyboard, 0x4F}}, {"PREV SLIDE", {HidKey::Keyboard, 0x50}},
      {"ESC", {HidKey::Keyboard, 0x29}}, {"ENTER", {HidKey::Keyboard, 0x28}}});
}
std::unique_ptr<Screen> makeKeys() {
  return std::make_unique<KeysScreen>("B1k", "KEYS", std::vector<Key>{
      {"UP", {HidKey::Keyboard, 0x52}}, {"DOWN", {HidKey::Keyboard, 0x51}}, {"LEFT", {HidKey::Keyboard, 0x50}},
      {"RIGHT", {HidKey::Keyboard, 0x4F}}, {"PAGE UP", {HidKey::Keyboard, 0x4B}}, {"PAGE DOWN", {HidKey::Keyboard, 0x4E}},
      {"ESC", {HidKey::Keyboard, 0x29}}, {"ENTER", {HidKey::Keyboard, 0x28}}});
}

std::unique_ptr<Screen> makeGroups() {
  return std::make_unique<MenuScreen>("B0", "REMOTE", [](App&) {
    std::vector<MenuItem> items(3);
    items[0].row.label = "MEDIA", items[0].onOk = [](App& a) { a.push(makeMedia()); };
    items[1].row.label = "PRESENTATION", items[1].onOk = [](App& a) { a.push(makePresentation()); };
    items[2].row.label = "KEYS", items[2].onOk = [](App& a) { a.push(makeKeys()); };
    return items;
  }, "Remote");
}

// ---- paired devices ----
std::unique_ptr<Screen> makeDetail(hal::BleBond b) {
  auto p = std::make_unique<PageScreen>("P2", tk::upper(b.name), "Device");
  p->kv([b](App& app) {
    const bool on = connected(app) && app.hal().ble.hostName() == b.name;
    return std::vector<tk::KV>{{"MAC", b.address.substr(0, 14)}, {"LAST USED", lastSeen(app, b.name)},
                               {"STATUS", on ? "CONNECTED" : "PAIRED"}};
  });
  p->onCancel([](App& a) { a.pop(); });
  return p;
}

std::unique_ptr<Screen> makePaired() {
  auto m = std::make_unique<MenuScreen>("P1", "PAIRED DEVICES", [](App& app) {
    std::vector<MenuItem> items;
    const std::string host = connected(app) ? app.hal().ble.hostName() : "";
    for (const auto& b : app.hal().ble.bonds()) {
      MenuItem it;
      it.row.label = tk::upper(b.name);
      if (b.name == host) it.row.trail = tk::Trail::Dot;
      it.onOk = [b](App& a) { a.push(makeDetail(b)); };
      const bool on = b.name == host;
      it.onManage = [b, on](App& a) {
        std::vector<std::string> lines{"FORGET " + tk::upper(b.name) + "?"};
        if (on) lines.push_back("(DISCONNECTS NOW)");
        a.push(std::make_unique<DialogScreen>("P1d", lines, [b](App& c) {
                 c.hal().ble.forget(b.name);
                 c.pop();
               }),
               fade(150));
      };
      items.push_back(it);
    }
    return items;
  }, "Paired devices");
  m->empty("NO DEVICES");
  return m;
}

// ---- menu ----
class BtMenu : public MenuScreen {
 public:
  BtMenu()
      : MenuScreen("B-M0", "BLUETOOTH", [](App& app) {
          std::vector<MenuItem> items;
          MenuItem status;
          status.row.info = true;
          status.row.disabled = true;
          status.row.label = connected(app) ? "CONNECTED: " + tk::upper(app.hal().ble.hostName()) : "WAITING FOR HOST...";
          items.push_back(status);
          MenuItem remote, paired;
          remote.row.label = "REMOTE CONTROL", remote.onOk = [](App& a) { a.push(makeGroups()); };
          paired.row.label = "PAIRED DEVICES", paired.onOk = [](App& a) { a.push(makePaired()); };
          items.push_back(remote);
          items.push_back(paired);
          if (connected(app)) {
            MenuItem d;
            d.row.label = "DISCONNECT";
            d.onOk = [](App& a) { a.hal().ble.disconnect(); };
            items.push_back(d);
          }
          return items;
        }, "Bluetooth") {
    bottomText([](App&) { return std::string(); });
  }
  const char* code() const override { return on_ ? "B-M1" : "B-M0"; }
  void onEnter(App& app) override {
    app.hal().ble.startAdvertising(kDeviceName);
    sync(app);
    MenuScreen::onEnter(app);
  }
  void onResume(App& app) override {
    sync(app);
    MenuScreen::onResume(app);
  }
  void onLeave(App& app) override {
    // stop advertising when nothing is connected; a connection stays
    if (!connected(app)) app.hal().ble.stop();
  }
  void onSystem(App& app, SysEvent e) override {
    if (e == SysEvent::BlePairRequest) {
      const std::string host = tk::upper(app.hal().ble.hostName());
      app.push(std::make_unique<DialogScreen>(
                   "B-P", std::vector<std::string>{"PAIR WITH", host + "?"},
                   [](App& a) {
                     a.hal().ble.confirmPairing(true);
                     a.pop();
                   },
                   [](App& a) {
                     a.hal().ble.confirmPairing(false);
                     a.pop();
                   },
                   "OK=PAIR  CANCEL=NO"),
               fade(150));
      return;
    }
    if (e == SysEvent::BleConnected) recordSeen(app);
    sync(app);
    reload(app);
  }
  void onTick(App& app) override {
    if (on_ != connected(app)) {
      sync(app);
      reload(app);
    }
  }

 private:
  void sync(App& app) { on_ = connected(app); }
  bool on_ = false;
};

}  // namespace

std::unique_ptr<Screen> makeMenu(App&) { return std::make_unique<BtMenu>(); }

}  // namespace bt
}  // namespace app

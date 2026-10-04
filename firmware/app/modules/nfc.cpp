// nfc.cpp - the NFC-new flow: N0 menu (PN532 woken here), Read R1-R5, Write W1-W5b, Emulate E1/E1p/E2,
// Saved Dumps D1/D1d/D2.
//
// Dumps live on the SD card as sd:/nfc/<NAME>.nfc (key=value lines, one "block=" line per block).
// MIFARE Classic cards are dumped sector by sector (a sector the reader can't open is kept as "??");
// other cards keep their UID only. A dump can be emulated when it is UID-only or a complete Classic dump;
// a Classic dump with missing sectors needs the keys (shown as the emulation note).
#include <cstdio>

#include "app/modules.h"
#include "app/widgets.h"

namespace app {
namespace nfc {

using hal::Button;

namespace {

constexpr const char* kDir = "/nfc";
constexpr uint32_t kSavedMs = 1200;
constexpr uint32_t kSectorReadMs = 60;  // one sector per 60 ms while reading

struct Dump {
  std::string name;
  hal::NfcCard card;
  bool classic() const { return card.type.find("Classic") != std::string::npos; }
  int sectors() const { return (int)card.blocks.size() / 4; }
  int sectorsRead() const {
    int n = 0;
    for (int s = 0; s < sectors(); s++) {
      bool ok = true;
      for (int b = 0; b < 4; b++) ok &= card.blocks[(size_t)(s * 4 + b)].find('?') == std::string::npos;
      n += ok;
    }
    return n;
  }
  bool emulatable() const { return !classic() || (sectors() > 0 && sectorsRead() == sectors()); }
};

std::string pathOf(const std::string& n) { return std::string(kDir) + "/" + n + ".nfc"; }

bool loadDump(App& app, const std::string& name, Dump& d) {
  std::string text;
  if (!app.hal().storage.read(hal::Volume::Sd, pathOf(name), text)) return false;
  d = Dump{};
  d.name = name;
  size_t p = 0;
  while (p < text.size()) {
    size_t e = text.find('\n', p);
    if (e == std::string::npos) e = text.size();
    std::string line = text.substr(p, e - p);
    p = e + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
    if (k == "type") d.card.type = v;
    else if (k == "uid") d.card.uid = v;
    else if (k == "block") d.card.blocks.push_back(v);
  }
  return !d.card.uid.empty();
}

bool saveDump(App& app, const Dump& d) {
  std::string t = "type=" + d.card.type + "\nuid=" + d.card.uid + "\n";
  for (const auto& b : d.card.blocks) t += "block=" + b + "\n";
  return app.hal().storage.write(hal::Volume::Sd, pathOf(d.name), t);
}

std::vector<Dump> dumps(App& app) {
  std::vector<std::string> files;
  std::vector<Dump> out;
  app.hal().storage.list(hal::Volume::Sd, kDir, files);
  for (const auto& f : files) {
    if (f.size() <= 4 || f.compare(f.size() - 4, 4, ".nfc") != 0) continue;
    Dump d;
    if (loadDump(app, f.substr(0, f.size() - 4), d)) out.push_back(d);
  }
  return out;
}

// "MIFARE Classic 1K" -> "MIFARE 1K" (what fits the value column)
std::string shortType(const std::string& t) {
  std::string s = t;
  const size_t p = s.find("Classic ");
  if (p != std::string::npos) s.erase(p, 8);
  return tk::upper(s);
}

std::string spaced(const std::string& hex) {
  std::string s;
  for (size_t i = 0; i < hex.size(); i += 2) s += (i ? " " : "") + hex.substr(i, 2);
  return s;
}

std::vector<tk::KV> details(const Dump& d) {
  std::vector<tk::KV> r{{"TYPE", shortType(d.card.type)},
                        {"UID", d.card.uid}};
  if (d.classic()) r.push_back({"SECTORS", std::to_string(d.sectorsRead()) + "/" + std::to_string(d.sectors())});
  else r.push_back({"DATA", "UID ONLY"});
  r.push_back({"EMULATE", d.emulatable() ? "YES" : "NO"});
  return r;
}

// From a card read off the reader: Classic keeps blocks, others only the UID.
Dump fromCard(const hal::NfcCard& c) {
  Dump d;
  d.card.uid = c.uid;
  d.card.type = c.type;
  if (d.classic()) d.card.blocks = c.blocks;
  return d;
}

// ---------------- read ----------------

std::unique_ptr<Screen> makeReadWaiting();

std::unique_ptr<Screen> makeResult(Dump d) {
  auto p = std::make_unique<PageScreen>("R3", "CARD", "Card");
  p->kv([d](App&) { return details(d); }).hint("OK=SAVE  CANCEL=DROP");
  p->onOk([d](App& app) {
    auto t = std::make_unique<TextInputScreen>(
        "R4", "NAME", TextInputScreen::kFileChars, "", 16,
        [d](App& a, const std::string& name) {
          Dump x = d;
          x.name = name;
          if (!saveDump(a, x)) {
            a.toast("SD NOT AVAILABLE");
            return;
          }
          auto s = std::make_unique<PageScreen>("R5", "", "Saved");
          s->icon("check_new").lines({"SAVED"}).noBottom();
          s->timer(kSavedMs, [](App& b) { b.popTo("R1"); });
          a.replace(std::move(s));
        },
        [](App& a) { a.pop(); });
    app.push(std::move(t));
  });
  p->onCancel([](App& app) { app.popTo("R1"); });
  return p;
}

class Reading : public Screen {
 public:
  explicit Reading(hal::NfcCard c) : card_(std::move(c)) {}
  const char* code() const override { return "R2"; }
  std::string title() const override { return "Reading"; }
  void onEnter(App& app) override { at_ = app.now(); }
  void onInput(App& app, const InputEvent& e) override {
    if (e.tap(Button::Cancel)) app.pop();
  }
  int total() const { return card_.type.find("Classic") != std::string::npos ? (int)card_.blocks.size() / 4 : 1; }
  void onTick(App& app) override {
    if (done_) return;
    hal::NfcCard now;
    if (!app.hal().nfc.card(now) || now.uid != card_.uid) {
      done_ = true;
      app.emit(SysEvent::NfcCardLost);
      return;
    }
    if ((int)((app.now() - at_) / kSectorReadMs) >= total()) {
      done_ = true;
      app.emit(SysEvent::NfcReadDone);
    }
  }
  void onSystem(App& app, SysEvent e) override {
    if (e == SysEvent::NfcCardLost) {
      app.toast("CARD LOST");
      app.pop();
    } else if (e == SysEvent::NfcReadDone) {
      app.beep(2600, 60);
      app.replace(makeResult(fromCard(card_)));
    }
  }
  void draw(App& app, ui::Framebuffer& fb) override {
    tk::titleBar(fb, "READING");
    tk::centredLines(fb, 46, {"UID " + spaced(card_.uid), tk::upper(card_.type)}, false, 12);
    const int n = total();
    int done = (int)((app.now() - at_) / kSectorReadMs);
    if (done > n) done = n;
    tk::progress(fb, 84, done, n, card_.type.find("Classic") != std::string::npos
                                      ? "SECTOR " + std::to_string(done) + "/" + std::to_string(n)
                                      : "READING UID");
  }

 private:
  hal::NfcCard card_;
  uint32_t at_ = 0;
  bool done_ = false;
};

// R1 and W3: "place card on back", polling while shown
class PlaceCard : public Screen {
 public:
  PlaceCard(const char* code, const char* title, std::function<void(App&, const hal::NfcCard&)> found)
      : code_(code), title_(title), found_(std::move(found)) {}
  const char* code() const override { return code_; }
  std::string title() const override { return title_[0] == 'R' ? "Read" : "Write"; }
  void onEnter(App& app) override { start(app); }
  void onResume(App& app) override { start(app); }
  void onLeave(App& app) override { app.hal().nfc.setPolling(false); }
  void onInput(App& app, const InputEvent& e) override {
    if (e.tap(Button::Cancel)) app.pop();
  }
  void onTick(App& app) override {
    // a card that was already there when the page opened must be taken away first
    hal::NfcCard c;
    const bool present = app.hal().nfc.card(c);
    if (!present) armed_ = true;
    if (present && armed_ && !fired_) {
      fired_ = true;
      card_ = c;
      app.emit(SysEvent::NfcCardFound);
    }
  }
  void onSystem(App& app, SysEvent e) override {
    if (e == SysEvent::NfcCardFound) found_(app, card_);
  }
  void draw(App& app, ui::Framebuffer& fb) override {
    tk::titleBar(fb, title_);
    tk::busy(fb, "nfc", {"PLACE CARD ON BACK"}, app.now() - at_);
  }

 private:
  void start(App& app) {
    app.hal().nfc.setPolling(true);
    at_ = app.now();
    armed_ = fired_ = false;
  }
  const char* code_;
  const char* title_;
  std::function<void(App&, const hal::NfcCard&)> found_;
  hal::NfcCard card_;
  uint32_t at_ = 0;
  bool armed_ = false, fired_ = false;
};

std::unique_ptr<Screen> makeReadWaiting() {
  return std::make_unique<PlaceCard>("R1", "READ", [](App& app, const hal::NfcCard& c) {
    app.push(std::make_unique<Reading>(c));
  });
}

// ---------------- write ----------------

std::unique_ptr<Screen> makeWriteResult(bool done, int skipped) {
  auto p = std::make_unique<PageScreen>("W5a", "RESULT", "Result");
  p->kv([done, skipped](App&) {
    return std::vector<tk::KV>{{"STATUS", done ? "DONE" : "STOPPED"},
                               {"SKIPPED", std::to_string(skipped) + (skipped == 1 ? " SECTOR" : " SECTORS")}};
  });
  p->hint("OK=BACK").onOk([](App& a) { a.popTo("W1"); });
  return p;
}

class Writing : public Screen {
 public:
  explicit Writing(Dump d) : d_(std::move(d)) {}
  const char* code() const override { return "W4"; }
  std::string title() const override { return "Writing"; }
  void onInput(App& app, const InputEvent& e) override {
    if (e.tap(Button::Cancel)) finish(app, false);
  }
  void onTick(App& app) override {
    if (finished_) return;
    // one block per loop tick
    if (next_ < (int)d_.card.blocks.size()) {
      const std::string& b = d_.card.blocks[(size_t)next_];
      const bool unread = b.find('?') != std::string::npos;
      if (unread || !app.hal().nfc.writeBlock(next_, b)) failed_.push_back(next_ / 4);
      next_++;
    }
    if (next_ >= (int)d_.card.blocks.size()) app.emit(SysEvent::NfcWriteDone);
  }
  void onSystem(App& app, SysEvent e) override {
    if (e == SysEvent::NfcWriteDone) finish(app, true);
  }
  void draw(App&, ui::Framebuffer& fb) override {
    tk::titleBar(fb, "WRITING");
    tk::centredLines(fb, 46, {d_.name});
    const int n = d_.sectors() ? d_.sectors() : 1;
    const int done = d_.sectors() ? next_ / 4 : (next_ > 0);
    tk::progress(fb, 84, done, n, "SECTOR " + std::to_string(done) + "/" + std::to_string(n));
  }

 private:
  void finish(App& app, bool done) {
    if (finished_) return;
    finished_ = true;
    int skipped = 0, last = -1;
    for (int s : failed_)
      if (s != last) skipped++, last = s;
    if (done) app.beep(2600, 60);
    app.replace(makeWriteResult(done, skipped));
  }
  Dump d_;
  int next_ = 0;
  bool finished_ = false;
  std::vector<int> failed_;
};

std::unique_ptr<Screen> makeWritePlace(Dump d) {
  return std::make_unique<PlaceCard>("W3", "WRITE", [d](App& app, const hal::NfcCard& c) {
    // "Card type matches?"
    if (c.type == d.card.type) {
      app.replace(std::make_unique<Writing>(d));
    } else {
      auto p = std::make_unique<PageScreen>("W5b", "", "Mismatch");
      p->lines({"CARD TYPE MISMATCH", "USE A " + shortType(d.card.type) + " CARD"}).linesY(64, 12).hint("OK=BACK");
      p->onOk([](App& a) { a.popTo("W1"); });
      app.replace(std::move(p));
    }
  });
}

std::unique_ptr<Screen> makeDumpPicker(const char* code, const char* title, std::function<void(App&, const Dump&)> pick) {
  auto m = std::make_unique<MenuScreen>(code, title, [pick](App& app) {
    std::vector<MenuItem> items;
    for (const Dump& d : dumps(app)) {
      MenuItem it;
      it.row.label = d.name;
      if (d.emulatable()) it.row.trail = tk::Trail::Check;
      it.onOk = [pick, d](App& a) { pick(a, d); };
      items.push_back(it);
    }
    return items;
  }, title[0] == 'W' ? "Write" : "Emulate");
  m->empty("NO DUMPS");
  return m;
}

std::unique_ptr<Screen> makeWritePick() {
  return makeDumpPicker("W1", "WRITE", [](App& app, const Dump& d) {
    app.push(std::make_unique<DialogScreen>("W2", std::vector<std::string>{"WRITE " + d.name, "TO CARD?", "UID STAYS THE SAME"},
                                            [d](App& a) { a.replace(makeWritePlace(d)); }),
             fade(150));
  });
}

// ---------------- emulate ----------------

class Emulating : public Screen {
 public:
  explicit Emulating(Dump d) : d_(std::move(d)) {}
  const char* code() const override { return "E2"; }
  std::string title() const override { return "Emulating"; }
  bool keepAwake() const override { return true; }  // idle sleep paused while emulating
  void onEnter(App& app) override {
    at_ = app.now();
    app.hal().nfc.startEmulation(d_.card);
  }
  void onLeave(App& app) override { app.hal().nfc.stopEmulation(); }
  void onInput(App& app, const InputEvent& e) override {
    if (e.tap(Button::Cancel)) app.pop();
  }
  void onTick(App& app) override {
    const int t = app.hal().nfc.readerTaps();
    if (t != taps_) {
      taps_ = t;
      app.beep(2200, 40);
    }
  }
  void draw(App& app, ui::Framebuffer& fb) override {
    tk::titleBar(fb, "EMULATE");
    tk::busy(fb, "nfc", {d_.name, "UID " + d_.card.uid}, app.now() - at_);
    tk::bottomBar(fb, "CANCEL=STOP");
  }

 private:
  Dump d_;
  uint32_t at_ = 0;
  int taps_ = 0;
};

std::unique_ptr<Screen> makeEmulatePick() {
  return makeDumpPicker("E1", "EMULATE", [](App& app, const Dump& d) {
    if (d.emulatable()) {
      app.push(std::make_unique<Emulating>(d));
    } else {
      app.push(std::make_unique<DialogScreen>("E1p",
                                              std::vector<std::string>{"CANNOT EMULATE:", "NEEDS FULL MIFARE", "AUTHENTICATION"},
                                              [](App& a) { a.pop(); }, [](App& a) { a.pop(); }, "OK=BACK"),
               fade(150));
    }
  });
}

// ---------------- saved ----------------

std::unique_ptr<Screen> makeSaved() {
  auto m = std::make_unique<MenuScreen>("D1", "SAVED DUMPS", [](App& app) {
    std::vector<MenuItem> items;
    for (const Dump& d : dumps(app)) {
      MenuItem it;
      it.row.label = d.name;
      if (d.emulatable()) it.row.trail = tk::Trail::Check;
      it.onOk = [d](App& a) {
        auto p = std::make_unique<PageScreen>("D2", d.name, "Dump");
        p->kv([d](App&) { return details(d); }).noBottom();
        p->onCancel([](App& b) { b.pop(); });
        a.push(std::move(p));
      };
      it.onManage = [d](App& a) {
        a.push(std::make_unique<DialogScreen>("D1d", std::vector<std::string>{"DELETE " + d.name + "?"},
                                              [d](App& b) {
                                                b.hal().storage.remove(hal::Volume::Sd, pathOf(d.name));
                                                b.pop();
                                              }),
               fade(150));
      };
      items.push_back(it);
    }
    return items;
  }, "Saved dumps");
  m->empty("NO DUMPS");
  return m;
}

class NfcMenu : public MenuScreen {
 public:
  NfcMenu()
      : MenuScreen("N0", "NFC", [this](App&) {
          std::vector<MenuItem> items;
          if (!present_) return items;
          items.resize(4);
          items[0].row.label = "READ CARD", items[0].onOk = [](App& a) { a.push(makeReadWaiting()); };
          items[1].row.label = "WRITE CARD", items[1].onOk = [](App& a) { a.push(makeWritePick()); };
          items[2].row.label = "EMULATE CARD", items[2].onOk = [](App& a) { a.push(makeEmulatePick()); };
          items[3].row.label = "SAVED DUMPS", items[3].onOk = [](App& a) { a.push(makeSaved()); };
          return items;
        }, "NFC") {
    empty("NFC MODULE NOT FOUND");
  }
  void onEnter(App& app) override {
    present_ = app.hal().nfc.begin();  // the PN532 starts here
    MenuScreen::onEnter(app);
  }

 private:
  bool present_ = false;
};

}  // namespace

std::unique_ptr<Screen> makeMenu(App&) { return std::make_unique<NfcMenu>(); }

}  // namespace nfc
}  // namespace app

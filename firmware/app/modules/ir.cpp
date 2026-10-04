// ir.cpp - the IR-new flow. Send: S1 categories (S1-0 when there are none), S1a category menu, S1b rename,
// S1c delete, S2 signals (S2-0 empty), S2t "Sent", S3 delete signal. Learn: L1 waiting (receiver on only
// here, ~15 s timeout), L1b no signal, L2a/L2b captured (decoded / RAW), L3 name, L4 pick category, L4n new
// category, L5 saved (then back to L1 for the next button).
//
// Signals live on the SD card, one file per category: sd:/ir/<CATEGORY>.ir, one signal per line:
//   NAME<TAB>PROTOCOL<TAB>ADDRESS<TAB>COMMAND<TAB>RAW (raw: comma-separated microseconds, RAW only)
#include <cstdio>
#include <cstdlib>

#include "app/modules.h"
#include "app/widgets.h"

namespace app {
namespace ir {

namespace {

constexpr const char* kDir = "/ir";
constexpr uint32_t kLearnTimeoutMs = 15000;
constexpr uint32_t kSentMs = 1000;
constexpr uint32_t kSavedMs = 1200;

struct Signal {
  std::string name;
  hal::IrSignal sig;
};

std::string pathOf(const std::string& cat) { return std::string(kDir) + "/" + cat + ".ir"; }

std::vector<std::string> categories(App& app) {
  std::vector<std::string> files, out;
  app.hal().storage.list(hal::Volume::Sd, kDir, files);
  for (const auto& f : files)
    if (f.size() > 3 && f.compare(f.size() - 3, 3, ".ir") == 0) out.push_back(f.substr(0, f.size() - 3));
  return out;
}

std::vector<Signal> loadCat(App& app, const std::string& cat) {
  std::vector<Signal> out;
  std::string text;
  if (!app.hal().storage.read(hal::Volume::Sd, pathOf(cat), text)) return out;
  size_t p = 0;
  while (p < text.size()) {
    size_t e = text.find('\n', p);
    if (e == std::string::npos) e = text.size();
    std::string line = text.substr(p, e - p);
    p = e + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::vector<std::string> f;
    size_t q = 0;
    while (true) {
      const size_t t = line.find('\t', q);
      f.push_back(line.substr(q, t == std::string::npos ? std::string::npos : t - q));
      if (t == std::string::npos) break;
      q = t + 1;
    }
    if (f.size() < 4 || f[0].empty()) continue;
    Signal s;
    s.name = f[0];
    s.sig.protocol = f[1];
    s.sig.address = (uint32_t)std::strtoul(f[2].c_str(), nullptr, 0);
    s.sig.command = (uint32_t)std::strtoul(f[3].c_str(), nullptr, 0);
    if (f.size() > 4) {
      const char* c = f[4].c_str();
      while (*c) {
        char* end;
        const unsigned long v = std::strtoul(c, &end, 10);
        if (end == c) break;
        s.sig.raw.push_back((uint16_t)v);
        c = *end == ',' ? end + 1 : end;
      }
    }
    out.push_back(s);
  }
  return out;
}

bool saveCat(App& app, const std::string& cat, const std::vector<Signal>& list) {
  std::string t;
  char num[32];
  for (const auto& s : list) {
    t += s.name + "\t" + s.sig.protocol + "\t";
    std::snprintf(num, sizeof(num), "0x%X\t0x%X\t", (unsigned)s.sig.address, (unsigned)s.sig.command);
    t += num;
    for (size_t i = 0; i < s.sig.raw.size(); i++) t += (i ? "," : "") + std::to_string(s.sig.raw[i]);
    t += "\n";
  }
  return app.hal().storage.write(hal::Volume::Sd, pathOf(cat), t);
}

std::string hex(uint32_t v) {
  char b[16];
  std::snprintf(b, sizeof(b), "0X%02X", (unsigned)v);
  return b;
}

// ---------------- send ----------------

std::unique_ptr<Screen> makeSignals(const std::string& cat);

class CategoryList : public MenuScreen {
 public:
  explicit CategoryList(const char* title, const char* code, Builder b) : MenuScreen(code, title, std::move(b), "Send") {}
  const char* code() const override { return rowsShown().empty() ? "S1-0" : "S1"; }
};

std::unique_ptr<Screen> makeCategoryMenu(const std::string& cat) {
  return std::make_unique<PopupScreen>("S1a", std::vector<std::string>{"RENAME", "DELETE"}, [cat](App& app, int i) {
    if (i == 0) {
      auto t = std::make_unique<TextInputScreen>(
          "S1b", "RENAME", TextInputScreen::kFileChars, cat, 16,
          [cat](App& a, const std::string& name) {
            if (name != cat && !a.hal().storage.rename(hal::Volume::Sd, pathOf(cat), pathOf(name))) {
              a.toast("CAN'T RENAME");
              return;
            }
            a.popTo("S1");
          },
          [](App& a) { a.popTo("S1"); });
      t->check([cat](App& a, const std::string& name) -> std::string {
        if (name == cat) return "";
        for (const auto& c : categories(a))
          if (c == name) return "NAME EXISTS";
        return "";
      });
      app.replace(std::move(t));
    } else {
      const size_t n = loadCat(app, cat).size();
      std::vector<std::string> lines;
      if (n) lines = {"DELETE " + cat, "AND " + std::to_string(n) + (n == 1 ? " REMOTE?" : " REMOTES?")};
      else lines = {"DELETE " + cat + "?"};
      app.replace(std::make_unique<DialogScreen>("S1c", lines, [cat](App& a) {
        a.hal().storage.remove(hal::Volume::Sd, pathOf(cat));
        a.pop();
      }));
    }
  });
}

std::unique_ptr<Screen> makeCategories() {
  auto m = std::make_unique<CategoryList>("IR / SEND", "S1", [](App& app) {
    std::vector<MenuItem> items;
    for (const auto& c : categories(app)) {
      MenuItem it;
      it.row.label = c;
      it.row.icon = "folder_new";
      it.onOk = [c](App& a) { a.push(makeSignals(c)); };
      it.onManage = [c](App& a) { a.push(makeCategoryMenu(c), fade(150)); };
      items.push_back(it);
    }
    return items;
  });
  m->empty("NO REMOTES");
  return m;
}

class SignalList : public MenuScreen {
 public:
  SignalList(const std::string& cat, Builder b) : MenuScreen("S2", cat, std::move(b), cat) {}
  const char* code() const override { return rowsShown().empty() ? "S2-0" : "S2"; }
};

std::unique_ptr<Screen> makeSignals(const std::string& cat) {
  auto m = std::make_unique<SignalList>(cat, [cat](App& app) {
    std::vector<MenuItem> items;
    const auto list = loadCat(app, cat);
    for (size_t i = 0; i < list.size(); i++) {
      MenuItem it;
      it.row.label = list[i].name;
      it.row.value = tk::upper(list[i].sig.protocol);
      it.row.valueGray = true;
      const Signal s = list[i];
      it.onOk = [s](App& a) {
        a.hal().ir.send(s.sig);
        a.beep(3000, 40);
        a.push(std::make_unique<ToastScreen>("S2t", "SENT", kSentMs));
      };
      const std::string name = list[i].name;
      it.onManage = [cat, name](App& a) {
        a.push(std::make_unique<DialogScreen>("S3", std::vector<std::string>{"DELETE " + name + "?"},
                                              [cat, name](App& b) {
                                                auto l = loadCat(b, cat);
                                                for (size_t k = 0; k < l.size(); k++)
                                                  if (l[k].name == name) l.erase(l.begin() + (long)k), k = l.size();
                                                saveCat(b, cat, l);
                                                b.pop();
                                              }),
               fade(150));
      };
      items.push_back(it);
    }
    return items;
  });
  m->empty("NO REMOTES");
  return m;
}

// ---------------- learn ----------------

std::unique_ptr<Screen> makeName(hal::IrSignal sig);

std::unique_ptr<Screen> makeSaved() {
  auto p = std::make_unique<PageScreen>("L5", "", "Saved");
  p->icon("check_new").lines({"SAVED"}).noBottom();
  p->timer(kSavedMs, [](App& a) { a.popTo("L1"); });
  return p;
}

bool store(App& app, const std::string& cat, const std::string& name, const hal::IrSignal& sig) {
  auto list = loadCat(app, cat);
  for (size_t k = 0; k < list.size(); k++)
    if (list[k].name == name) list.erase(list.begin() + (long)k), k = list.size();
  list.push_back({name, sig});
  return saveCat(app, cat, list);
}

std::unique_ptr<Screen> makePickCategory(std::string name, hal::IrSignal sig) {
  return std::make_unique<MenuScreen>("L4", "CATEGORY", [name, sig](App& app) {
    std::vector<MenuItem> items;
    for (const auto& c : categories(app)) {
      MenuItem it;
      it.row.label = c;
      it.row.icon = "folder_new";
      it.onOk = [c, name, sig](App& a) {
        if (!store(a, c, name, sig)) {
          a.toast("SD NOT AVAILABLE");
          return;
        }
        a.replace(makeSaved());
      };
      items.push_back(it);
    }
    MenuItem add;
    add.row.label = "NEW CATEGORY";
    add.row.icon = "plus_new";
    add.onOk = [name, sig](App& a) {
      auto t = std::make_unique<TextInputScreen>(
          "L4n", "NAME", TextInputScreen::kFileChars, "", 16,
          [name, sig](App& b, const std::string& cat) {
            if (!store(b, cat, name, sig)) {
              b.toast("SD NOT AVAILABLE");
              return;
            }
            b.popTo("L4");
            b.replace(makeSaved());
          },
          [](App& b) { b.pop(); });
      t->check([](App& b, const std::string& cat) -> std::string {
        for (const auto& c : categories(b))
          if (c == cat) return "NAME EXISTS";
        return "";
      });
      a.push(std::move(t));
    };
    items.push_back(add);
    return items;
  }, "Category");
}

std::unique_ptr<Screen> makeName(hal::IrSignal sig) {
  return std::make_unique<TextInputScreen>(
      "L3", "NAME", TextInputScreen::kFileChars, "", 16,
      [sig](App& app, const std::string& name) { app.push(makePickCategory(name, sig)); },
      [](App& app) { app.popTo("L1"); });
}

std::unique_ptr<Screen> makeCaptured(hal::IrSignal sig) {
  const bool raw = sig.protocol == "RAW";
  auto p = std::make_unique<PageScreen>(raw ? "L2b" : "L2a", "CAPTURED", "Captured");
  p->kv([sig, raw](App&) {
    if (raw) return std::vector<tk::KV>{{"PROTOCOL", "RAW"}, {"PULSES", std::to_string(sig.raw.size())}};
    return std::vector<tk::KV>{{"PROTOCOL", tk::upper(sig.protocol)}, {"ADDRESS", hex(sig.address)}, {"COMMAND", hex(sig.command)}};
  });
  p->hint("OK=NAME  CANCEL=DROP");
  p->onOk([sig](App& a) { a.push(makeName(sig)); });
  p->onCancel([](App& a) { a.pop(); });
  return p;
}

class LearnScreen : public Screen {
 public:
  const char* code() const override { return "L1"; }
  std::string title() const override { return "Learn"; }
  void onEnter(App& app) override { listen(app); }
  void onResume(App& app) override { listen(app); }
  void onLeave(App& app) override { app.hal().ir.setListening(false); }
  void onInput(App& app, const InputEvent& e) override {
    if (e.tap(hal::Button::Cancel)) app.pop();  // receiver off (onLeave)
  }
  void onTick(App& app) override {
    if (!listening_) return;
    if (app.hal().ir.receive(got_)) {
      stop(app);
      app.emit(SysEvent::IrReceived);
    } else if (app.now() - since_ >= kLearnTimeoutMs) {
      stop(app);
      app.emit(SysEvent::IrLearnTimeout);
    }
  }
  void onSystem(App& app, SysEvent e) override {
    if (e == SysEvent::IrReceived) {
      app.beep(2400, 40);
      app.push(makeCaptured(got_));  // "Protocol decoded?" picks L2a or L2b
    } else if (e == SysEvent::IrLearnTimeout) {
      auto p = std::make_unique<PageScreen>("L1b", "", "No signal");
      p->lines({"NO SIGNAL", "TRY AGAIN"}).linesY(64, 12).hint("OK=RETRY  CANCEL=BACK");
      p->onOk([](App& a) { a.pop(); });
      p->onCancel([](App& a) { a.popTo("I0"); });
      app.push(std::move(p));
    }
  }
  void draw(App& app, ui::Framebuffer& fb) override {
    tk::titleBar(fb, "LEARN");
    tk::busy(fb, "ir", {"POINT REMOTE AT", "RECEIVER"}, app.now() - since_);
  }

 private:
  void listen(App& app) {
    listening_ = true;
    since_ = app.now();
    app.hal().ir.setListening(true);
  }
  void stop(App& app) {
    listening_ = false;
    app.hal().ir.setListening(false);
  }
  bool listening_ = false;
  uint32_t since_ = 0;
  hal::IrSignal got_;
};

}  // namespace

std::unique_ptr<Screen> makeMenu(App&) {
  return std::make_unique<MenuScreen>("I0", "IR", [](App&) {
    std::vector<MenuItem> items(2);
    items[0].row.label = "SEND", items[0].onOk = [](App& a) { a.push(makeCategories()); };
    items[1].row.label = "LEARN", items[1].onOk = [](App& a) { a.push(std::make_unique<LearnScreen>()); };
    return items;
  }, "IR");
}

}  // namespace ir
}  // namespace app

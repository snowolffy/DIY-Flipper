#include "app/app_host.h"

#include "app/theme.h"
#include "app/toolkit.h"
#include "app/widgets.h"

namespace apps {

using app::App;
using app::InputEvent;
using hal::Button;

// ---------------- host services ----------------

namespace {

std::string savePath(App& app, const std::string& id, hal::Volume& v) {
  v = app.hal().storage.present(hal::Volume::Sd) ? hal::Volume::Sd : hal::Volume::Flash;
  return v == hal::Volume::Sd ? "/games/save/" + id + ".ini" : "/games/" + id + ".ini";
}

}  // namespace

bool Host::save(const std::string& key, const std::string& value) {
  hal::Volume v;
  const std::string path = savePath(app_, id_, v);
  std::string text, out;
  app_.hal().storage.read(v, path, text);
  size_t p = 0;
  while (p < text.size()) {
    size_t e = text.find('\n', p);
    if (e == std::string::npos) e = text.size();
    const std::string line = text.substr(p, e - p);
    if (!line.empty() && line.compare(0, key.size() + 1, key + "=") != 0) out += line + "\n";
    p = e + 1;
  }
  out += key + "=" + value + "\n";
  return app_.hal().storage.write(v, path, out);
}

std::string Host::load(const std::string& key, const std::string& fallback) {
  hal::Volume v;
  const std::string path = savePath(app_, id_, v);
  std::string text;
  if (!app_.hal().storage.read(v, path, text)) return fallback;
  const size_t p = text.find(key + "=");
  if (p == std::string::npos || (p > 0 && text[p - 1] != '\n')) return fallback;
  const size_t e = text.find('\n', p);
  return text.substr(p + key.size() + 1, (e == std::string::npos ? text.size() : e) - p - key.size() - 1);
}

uint32_t Host::random() {
  seed_ = seed_ * 1103515245u + 12345u;  // deterministic: same game, same run
  return seed_ >> 8;
}

// ---------------- packs ----------------

namespace {

std::string field(const std::string& text, const std::string& key, std::vector<std::string>* all = nullptr) {
  std::string first;
  size_t p = 0;
  while (p < text.size()) {
    size_t e = text.find('\n', p);
    if (e == std::string::npos) e = text.size();
    std::string line = text.substr(p, e - p);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.compare(0, key.size() + 1, key + "=") == 0) {
      const std::string v = line.substr(key.size() + 1);
      if (first.empty()) first = v;
      if (all) all->push_back(v);
    }
    p = e + 1;
  }
  return first;
}

}  // namespace

std::vector<std::string> packDirs(App& app) {
  std::vector<std::string> dirs, out;
  app.hal().storage.listDirs(hal::Volume::Sd, "/games", dirs);
  for (const auto& d : dirs)
    if (d != "save" && app.hal().storage.exists(hal::Volume::Sd, "/games/" + d + "/manifest.ini")) out.push_back(d);
  return out;
}

std::string packName(App& app, const std::string& dir) {
  std::string m;
  app.hal().storage.read(hal::Volume::Sd, "/games/" + dir + "/manifest.ini", m);
  const std::string n = field(m, "name");
  return n.empty() ? dir : n;
}

bool validatePack(App& app, const std::string& dir, PackInfo& out, std::string& reason) {
  hal::Storage& st = app.hal().storage;
  const std::string base = "/games/" + dir;
  std::string m;
  if (!st.read(hal::Volume::Sd, base + "/manifest.ini", m)) {
    reason = "NO MANIFEST";
    return false;
  }
  out = PackInfo{};
  out.dir = dir;
  out.name = field(m, "name");
  if (out.name.empty()) out.name = dir;
  const std::string type = field(m, "type");
  out.engine = field(m, "engine");
  if (type == "canvas") out.type = AppType::Canvas;
  else if (type == "screens") out.type = AppType::Screens;
  else {
    reason = "BAD PACK FILE";
    return false;
  }
  // packs on the SD card always run at level 0
  const std::string bypass = field(m, "bypass");
  if (!bypass.empty() && bypass != "0") {
    reason = "PACKS CAN'T BYPASS";
    return false;
  }
  std::vector<std::string> binds;
  field(m, "bind", &binds);
  for (const auto& b : binds)
    if (b.compare(0, 11, "cancel_hold") == 0) {
      reason = "CANCEL HOLD RESERVED";
      return false;
    }
  if (out.type == AppType::Canvas) {
    if (out.engine != "sprite2d") {
      reason = "UNKNOWN ENGINE";
      return false;
    }
    std::vector<std::string> files;
    st.list(hal::Volume::Sd, base, files);
    int count = 0;
    uint32_t bytes = 0;
    for (const auto& f : files) {
      if (f.size() < 4 || f.compare(f.size() - 4, 4, ".c16") != 0) continue;
      std::string data, err;
      theme::Image img;
      if (!st.read(hal::Volume::Sd, base + "/" + f, data) || !theme::parseC16(data, img, err)) {
        reason = "BAD IMAGE";
        return false;
      }
      if (img.w > kMaxSpriteW || img.h > kMaxSpriteH) {
        reason = "IMAGE TOO BIG";
        return false;
      }
      count++;
      bytes += (uint32_t)img.px.size() * 2;
    }
    if (count > kMaxPackAssets || bytes > kPackPsramBudget) {
      reason = "TOO MANY ASSETS";
      return false;
    }
    out.image = field(m, "scene");
    if (out.image.empty() || !st.exists(hal::Volume::Sd, base + "/" + out.image)) {
      reason = "NO SCENE IMAGE";
      return false;
    }
  } else {
    if (out.engine != "menu_flow") {
      reason = "UNKNOWN ENGINE";
      return false;
    }
    field(m, "page", &out.pages);
    if (out.pages.empty()) {
      reason = "NO PAGES";
      return false;
    }
    for (const auto& pg : out.pages) {
      const std::string kind = pg.substr(0, pg.find('|'));
      if (kind != "list" && kind != "message" && kind != "detail") {
        reason = "NOT A STANDARD PAGE";
        return false;
      }
    }
  }
  return true;
}

// ---------------- the host page (G3) ----------------

namespace {

class HostScreen : public app::Screen {
 public:
  HostScreen(App& app, AppEntry e) : e_(std::move(e)), host_(app, e_.id) {}
  const char* code() const override { return "G3"; }
  std::string title() const override { return e_.label; }
  bool statusBar() const override { return false; }
  bool keepAwake() const override { return true; }
  bool hostsApp() const override { return true; }
  bool rawInput() const override { return e_.level == Bypass::RawInput; }
  // a short Cancel reaches the app on release, because Cancel hold is the host's (levels 0 and 1)
  uint8_t deferMask() const override { return app::bit(Button::Cancel); }

  void onEnter(App&) override { start(); }
  void onResume(App&) override {
    if (restart_) {
      restart_ = false;
      finishApp();
      start();
    }
  }
  void onInput(App& app, const InputEvent& e) override {
    if (!app_ || finished_) return;
    if (e.hold(Button::Cancel)) {
      if (e_.level == Bypass::CustomPause && e_.pause) e_.pause(*app_, host_);
      else openPause(app);
      return;
    }
    app_->onInput(host_, e);
  }
  void onRaw(App&, const app::RawEvent& r) override {
    if (app_ && !finished_ && e_.raw) e_.raw(*app_, host_, r);
  }
  void onTick(App& app) override {
    if (!app_ || finished_) return;
    app_->onTick(host_);
    const int pct = app.battery().percent();
    if (pct >= 0 && pct < kSaveBelowPct && !saveAsked_) {
      saveAsked_ = true;
      app_->onSaveRequest(host_);
      auto w = std::make_unique<app::PageScreen>("G-bat", "");
      w->noStatus().lines({"BATTERY LOW", "GAME SAVED"}).linesY(64, 12).hint("OK=BACK");
      w->onOk([](App& a) { a.pop(); });
      app.push(std::move(w));
      return;
    }
    if (host_.exitRequested()) leave(app);
  }
  void onSystem(App& app, app::SysEvent ev) override {
    if (ev != app::SysEvent::AppFailsafe) return;
    // forced exit: onExit gets its budget, then G5 for half a second
    finishApp();
    auto g5 = std::make_unique<app::PageScreen>("G5", "");
    g5->noStatus().noBottom();
    g5->custom([](App&, ui::Framebuffer& fb) {
      fb.fillRect(0, 148, ui::kScreenW, 12, ui::color::kBlack);
      ui::drawTextCentered(fb, theme::small(), 150, "EXITING...", ui::color::kWhite);
      fb.fillRect(0, 158, ui::kScreenW, 2, ui::color::kWhite);
    });
    g5->timer(kFailsafeExitMs, [](App& a) { a.popTo("G1"); });
    app.push(std::move(g5));
  }
  void draw(App&, ui::Framebuffer& fb) override {
    if (app_) app_->draw(host_, fb);
    else fb.fillRect(ui::kScreenRect, ui::color::kLight);
  }

  // pause menu actions
  void resume() {}
  void restart() { restart_ = true; }
  void leave(App& app) {
    finishApp();
    app.popTo("G1");
  }

 private:
  void start() {
    app_ = e_.make();
    finished_ = false;
    host_.clearExit();
    app_->onStart(host_);
  }
  void finishApp() {
    if (!app_ || finished_) return;
    finished_ = true;
    app_->onExit(host_);  // single-threaded: the budget is reported, not enforced by preemption
  }
  void openPause(App& app) {
    HostScreen* self = this;
    app.push(std::make_unique<app::PopupScreen>("G4", std::vector<std::string>{"RESUME", "RESTART", "EXIT"},
                                                [self](App& a, int i) {
                                                  if (i == 1) self->restart();
                                                  a.pop();
                                                  if (i == 2) self->leave(a);
                                                }),
             app::fade(150));
  }

  AppEntry e_;
  Host host_;
  std::unique_ptr<CanvasApp> app_;
  bool finished_ = false, restart_ = false, saveAsked_ = false;
};

}  // namespace

std::unique_ptr<app::Screen> makeHostScreen(App& app, const AppEntry& e) { return std::make_unique<HostScreen>(app, e); }

}  // namespace apps

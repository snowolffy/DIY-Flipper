// games.cpp - the Games-new flow: G1 games menu (built-in first, then SD packs), G2 loading a pack
// (validatePack), G2f load failed, G3 the game (app_host.cpp), G4 pause menu, G5 failsafe exit.
//
// Built-in games, one per bypass level: Snake (0), Pong (1, draws its own pause), Reaction (2, raw input).
#include <cstdio>

#include "app/app_host.h"
#include "app/modules.h"
#include "app/theme.h"
#include "app/widgets.h"
#include "ui/gfx.h"

namespace app {
namespace games {

using apps::Host;
using hal::Button;
namespace col = ui::color;

namespace {

void text(ui::Framebuffer& fb, int16_t x, int16_t y, const std::string& s, ui::Color c) {
  ui::drawText(fb, theme::small(), x, y, s.c_str(), c);
}

// ---------------- Snake (level 0) ----------------
class Snake : public apps::CanvasApp {
 public:
  static constexpr const char* kId = "snake";
  static constexpr int kCell = 4, kW = 32, kH = 37;  // play field 128x148 below the score line

  void onStart(Host& h) override {
    best_ = std::atoi(h.load("best", "0").c_str());
    body_.clear();
    for (int i = 0; i < 4; i++) body_.push_back({10 - i, 18});
    dir_ = 0;
    score_ = 0;
    dead_ = false;
    last_ = h.now();
    food(h);
  }
  void onInput(Host& h, const InputEvent& e) override {
    // two buttons steer: < turns left, > turns right; OK restarts after a crash
    if (e.tap(Button::Left)) dir_ = (dir_ + 3) % 4;
    else if (e.tap(Button::Right)) dir_ = (dir_ + 1) % 4;
    else if (e.tap(Button::Ok) && dead_) onStart(h);
  }
  void onTick(Host& h) override {
    if (dead_ || h.now() - last_ < 120) return;
    last_ = h.now();
    static const int dx[] = {1, 0, -1, 0}, dy[] = {0, 1, 0, -1};
    P head{body_[0].x + dx[dir_], body_[0].y + dy[dir_]};
    bool hit = head.x < 0 || head.y < 0 || head.x >= kW || head.y >= kH;
    for (const P& p : body_) hit |= p.x == head.x && p.y == head.y;
    if (hit) {
      dead_ = true;
      h.beep(400, 200);
      if (score_ > best_) best_ = score_;
      return;
    }
    body_.insert(body_.begin(), head);
    if (head.x == food_.x && head.y == food_.y) {
      score_ += 10;
      h.beep(3000, 20);
      food(h);
    } else {
      body_.pop_back();
    }
  }
  void draw(Host&, ui::Framebuffer& fb) override {
    fb.clear();
    text(fb, 2, 2, "SCORE " + std::to_string(score_), col::kWhite);
    text(fb, 80, 2, "HI " + std::to_string(best_), col::kGray);
    fb.hline(0, 11, 128, col::kDark);
    for (const P& p : body_) fb.fillRect((int16_t)(p.x * kCell), (int16_t)(12 + p.y * kCell), kCell - 1, kCell - 1, col::kWhite);
    fb.fillRect((int16_t)(food_.x * kCell), (int16_t)(12 + food_.y * kCell), kCell - 1, kCell - 1, col::kLight);
    if (dead_) ui::drawTextCentered(fb, theme::small(), 76, "GAME OVER  OK=AGAIN", col::kWhite);
  }
  void onExit(Host& h) override { save(h); }
  void onSaveRequest(Host& h) override { save(h); }

 private:
  struct P {
    int x, y;
  };
  void save(Host& h) {
    if (score_ > best_) best_ = score_;
    h.save("best", std::to_string(best_));
  }
  void food(Host& h) {
    for (;;) {
      food_ = {(int)(h.random() % kW), (int)(h.random() % kH)};
      bool onBody = false;
      for (const P& p : body_) onBody |= p.x == food_.x && p.y == food_.y;
      if (!onBody) return;
    }
  }
  std::vector<P> body_;
  P food_{0, 0};
  int dir_ = 0, score_ = 0, best_ = 0;
  bool dead_ = false;
  uint32_t last_ = 0;
};

// ---------------- Pong (level 1: its own pause) ----------------
class Pong : public apps::CanvasApp {
 public:
  static constexpr const char* kId = "pong";
  void onStart(Host& h) override {
    paddle_ = 52;
    bx_ = 64 * 16, by_ = 40 * 16, vx_ = 23, vy_ = 31;
    score_ = 0;
    paused_ = false;
    last_ = h.now();
  }
  void onInput(Host& h, const InputEvent& e) override {
    if (paused_) {
      // OK resumes, Cancel leaves (the app ends itself at this level)
      if (e.tap(Button::Ok)) paused_ = false;
      else if (e.tap(Button::Cancel)) h.exit();
      return;
    }
    if (e.step(Button::Left)) paddle_ = paddle_ > 6 ? paddle_ - 6 : 0;
    if (e.step(Button::Right)) paddle_ = paddle_ < 98 ? paddle_ + 6 : 104;
  }
  void onPauseRequest(Host&) { paused_ = true; }
  void onTick(Host& h) override {
    if (paused_ || h.now() - last_ < 20) return;
    last_ = h.now();
    bx_ += vx_, by_ += vy_;
    if (bx_ < 0 || bx_ > 124 * 16) vx_ = -vx_, bx_ += 2 * vx_;
    if (by_ < 12 * 16) vy_ = -vy_, by_ += 2 * vy_;
    if (by_ >= 150 * 16) {
      const int x = bx_ / 16;
      if (x + 4 >= paddle_ && x <= paddle_ + 24) {
        vy_ = -vy_;
        by_ += 2 * vy_;
        score_++;
        h.beep(2500, 15);
      } else {
        h.beep(300, 150);
        onStart(h);
      }
    }
  }
  void draw(Host&, ui::Framebuffer& fb) override {
    fb.clear();
    text(fb, 2, 2, "PONG " + std::to_string(score_), col::kWhite);
    fb.hline(0, 11, 128, col::kDark);
    fb.fillRect((int16_t)paddle_, 154, 24, 3, col::kWhite);
    fb.fillRect((int16_t)(bx_ / 16), (int16_t)(by_ / 16), 4, 4, col::kLight);
    if (paused_) {
      fb.dim();
      tk::dialog(fb, {"PONG PAUSED"}, "OK=GO  CANCEL=QUIT");
    }
  }

 private:
  int paddle_ = 52, bx_ = 0, by_ = 0, vx_ = 0, vy_ = 0, score_ = 0;
  bool paused_ = false;
  uint32_t last_ = 0;
};

// ---------------- Reaction (level 2: raw input) ----------------
class Reaction : public apps::CanvasApp {
 public:
  static constexpr const char* kId = "reaction";
  void onStart(Host& h) override {
    goAt_ = h.now() + 1000 + h.random() % 2000;
    result_ = -1;
  }
  void onRawInput(Host& h, const RawEvent& r) {
    if (!r.down) return;
    if (r.button == Button::Cancel && result_ != -1) {
      h.exit();
      return;
    }
    if (result_ >= 0 || result_ == -2) {
      onStart(h);
      return;
    }
    result_ = (int32_t)(r.atMs - goAt_) < 0 ? -2 : (int)(r.atMs - goAt_);
  }
  void draw(Host& h, ui::Framebuffer& fb) override {
    const bool go = (int32_t)(h.now() - goAt_) >= 0;
    fb.fillRect(ui::kScreenRect, go && result_ == -1 ? col::kWhite : col::kBlack);
    const char* msg = result_ == -2 ? "TOO SOON" : result_ >= 0 ? "" : go ? "PRESS!" : "WAIT...";
    ui::drawTextCentered(fb, theme::small(), 70, msg, go && result_ == -1 ? col::kBlack : col::kWhite);
    if (result_ >= 0) {
      ui::drawTextCentered(fb, theme::small(), 70, (std::to_string(result_) + " MS").c_str(), col::kWhite);
      ui::drawTextCentered(fb, theme::small(), 90, "ANY=AGAIN  CANCEL=QUIT", col::kGray);
    }
  }

 private:
  uint32_t goAt_ = 0;
  int result_ = -1;  // -1 waiting, -2 too soon, else ms
};

// ---------------- SD packs ----------------
class SceneApp : public apps::CanvasApp {
 public:
  explicit SceneApp(theme::Image img) : img_(std::move(img)) {}
  void onInput(Host&, const InputEvent& e) override {
    if (e.step(Button::Left)) x_ -= 2;
    if (e.step(Button::Right)) x_ += 2;
  }
  void draw(Host&, ui::Framebuffer& fb) override {
    fb.clear();
    const PicEntry p{"scene", img_.px.data(), img_.w, img_.h};
    ui::drawPic(fb, (int16_t)(ui::centerIn(0, 128, (int16_t)img_.w) + x_), ui::centerIn(0, 160, (int16_t)img_.h), p);
  }

 private:
  theme::Image img_;
  int x_ = 0;
};

std::unique_ptr<Screen> makeScreensPack(const apps::PackInfo& info) {
  // menu_flow: page 0 is a list; its items open the other pages by index ("list|TITLE|ITEM>1;ITEM>2")
  std::vector<std::vector<std::string>> pages;
  for (const auto& pg : info.pages) {
    std::vector<std::string> f;
    size_t q = 0;
    while (true) {
      const size_t t = pg.find('|', q);
      f.push_back(pg.substr(q, t == std::string::npos ? std::string::npos : t - q));
      if (t == std::string::npos) break;
      q = t + 1;
    }
    pages.push_back(f);
  }
  return std::make_unique<MenuScreen>("G3", tk::upper(pages[0].size() > 1 ? pages[0][1] : info.name), [pages](App&) {
    std::vector<MenuItem> items;
    const std::string list = pages[0].size() > 2 ? pages[0][2] : "";
    size_t q = 0;
    while (q <= list.size() && !list.empty()) {
      size_t t = list.find(';', q);
      const std::string item = list.substr(q, t == std::string::npos ? std::string::npos : t - q);
      const size_t gt = item.find('>');
      MenuItem it;
      it.row.label = tk::upper(item.substr(0, gt));
      const int target = gt == std::string::npos ? -1 : std::atoi(item.c_str() + gt + 1);
      if (target > 0 && target < (int)pages.size()) {
        const auto pg = pages[(size_t)target];
        it.onOk = [pg](App& a) {
          auto p = std::make_unique<PageScreen>("G3", tk::upper(pg.size() > 1 ? pg[1] : ""));
          p->lines(tk::wrap(tk::upper(pg.size() > 2 ? pg[2] : ""), 20)).linesY(40, 12);
          p->onCancel([](App& b) { b.pop(); });
          a.push(std::move(p));
        };
      }
      items.push_back(it);
      if (t == std::string::npos) break;
      q = t + 1;
    }
    return items;
  }, info.name);
}

std::unique_ptr<Screen> makeLoading(const std::string& dir, const std::string& name) {
  auto p = std::make_unique<PageScreen>("G2", "GAMES", "Loading");
  p->busy().lines({"LOADING", name}).noBottom();
  // validate after a short moment so the page is seen; then pack_loaded / pack_failed
  auto info = std::make_shared<apps::PackInfo>();
  auto reason = std::make_shared<std::string>();
  p->tick([dir, info, reason, done = false](App& app, PageScreen& s) mutable {
    if (done || app.now() - s.enteredAt() < 600) return;
    done = true;
    app.emit(apps::validatePack(app, dir, *info, *reason) ? SysEvent::PackLoaded : SysEvent::PackFailed);
  });
  p->system([info, reason](App& app, SysEvent e) {
    if (e == SysEvent::PackFailed) {
      auto f = std::make_unique<PageScreen>("G2f", "");
      f->lines({"FAILED TO LOAD", *reason}).linesY(64, 12).hint("OK=BACK");
      f->onOk([](App& a) { a.popTo("G1"); });
      app.replace(std::move(f));
    } else if (e == SysEvent::PackLoaded) {
      if (info->type == apps::AppType::Screens) {
        app.replace(makeScreensPack(*info));
        return;
      }
      std::string data, err;
      theme::Image img;
      app.hal().storage.read(hal::Volume::Sd, "/games/" + info->dir + "/" + info->image, data);
      theme::parseC16(data, img, err);
      apps::AppEntry e2;
      e2.id = "pack_" + info->dir;
      e2.label = info->name;
      e2.level = apps::Bypass::Default;  // packs are always level 0
      e2.make = [img] { return std::unique_ptr<apps::CanvasApp>(new SceneApp(img)); };
      app.replace(apps::makeHostScreen(app, e2));
    }
  });
  p->onCancel([](App& a) { a.pop(); });
  return p;
}

const std::vector<apps::AppEntry>& builtins() {
  static const std::vector<apps::AppEntry> list = {
      apps::builtin<Snake>("SNAKE"),
      apps::builtin<Pong>("PONG"),
      apps::builtin<Reaction>("REACTION"),
  };
  return list;
}

}  // namespace

class GamesMenu : public MenuScreen {
 public:
  using MenuScreen::MenuScreen;
  void onEnter(App& app) override {
    MenuScreen::onEnter(app);
    if (!app.hal().storage.present(hal::Volume::Sd)) app.toast("SD NOT AVAILABLE");
  }
};

std::unique_ptr<Screen> makeMenu(App&) {
  auto m = std::make_unique<GamesMenu>("G1", "GAMES", [](App& app) {
    std::vector<MenuItem> items;
    for (const auto& e : builtins()) {
      MenuItem it;
      it.row.label = e.label;
      it.onOk = [e](App& a) { a.push(apps::makeHostScreen(a, e)); };
      items.push_back(it);
    }
    for (const auto& d : apps::packDirs(app)) {
      MenuItem it;
      const std::string name = tk::upper(apps::packName(app, d));
      it.row.label = name;
      it.onOk = [d, name](App& a) { a.push(makeLoading(d, name)); };
      items.push_back(it);
    }
    return items;
  }, "Games");
  return m;
}

}  // namespace games
}  // namespace app

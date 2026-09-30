// screens.h - the reusable screen templates (List, Detail) plus the boot splash, and the builders for the
// menus that exist so far.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "app/app.h"
#include "ui/gfx.h"

namespace app {

class SplashScreen : public Screen {
 public:
  static constexpr uint32_t kShowMs = 1500;
  const char* title() const override { return "Splash"; }
  bool hasStatusBar() const override { return false; }
  void onEnter(App& app) override;
  void onEvent(App& app, const ButtonEvent& e) override;
  void onTick(App& app) override;
  void draw(App& app, ui::Framebuffer& fb) override;

 private:
  uint32_t shownAt_ = 0;
  bool done_ = false;
};

class ListScreen : public Screen {
 public:
  struct Item {
    std::string label;
    const PicEntry* icon = nullptr;
    std::function<void(App&)> onOk;
    std::function<std::string(App&)> value;  // optional, drawn right-aligned in the small font
  };

  // Marquee timing for labels longer than the row: pause, one character per step, pause, restart.
  static constexpr uint32_t kMarqueePauseMs = 1000;
  static constexpr uint32_t kMarqueeStepMs = 250;

  ListScreen(std::string title, std::vector<Item> items) : title_(std::move(title)), items_(std::move(items)) {}
  const char* title() const override { return title_.c_str(); }
  void onEnter(App& app) override;
  void onEvent(App& app, const ButtonEvent& e) override;
  void draw(App& app, ui::Framebuffer& fb) override;

  int selected() const { return sel_; }

 private:
  void select(App& app, int index);
  int marqueeOffset(uint32_t now, int overflowChars) const;

  std::string title_;
  std::vector<Item> items_;
  int sel_ = 0;
  int first_ = 0;
  uint32_t selectedAt_ = 0;
};

class DetailScreen : public Screen {
 public:
  using Rows = std::vector<std::pair<std::string, std::string>>;

  DetailScreen(std::string title, std::function<Rows(App&)> rows)
      : title_(std::move(title)), rows_(std::move(rows)) {}
  const char* title() const override { return title_.c_str(); }
  bool hasTitleRow() const override { return true; }
  void onEvent(App& app, const ButtonEvent& e) override;
  void draw(App& app, ui::Framebuffer& fb) override;

 private:
  std::string title_;
  std::function<Rows(App&)> rows_;
};

std::unique_ptr<Screen> makeMainMenu();
std::unique_ptr<Screen> makeSettingsMenu();

}  // namespace app

// app_host.h - runs canvas apps (built-in games, SD packs) under app_rules.h.
//
// A built-in app is a class with a static constexpr kId and the CanvasApp callbacks. It is registered with
// apps::builtin<T>(), which looks its bypass level up in kBypassList and refuses to compile when a level-1
// app has no onPauseRequest() or a level-2 app has no onRawInput().
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "app/app.h"
#include "app/app_rules.h"
#include "ui/framebuffer.h"

namespace apps {

class Host;

// What an app gets: drawing, time, sound, a small save store, exit. No HAL - peripherals only through here.
class Host {
 public:
  Host(app::App& app, std::string id) : app_(app), id_(std::move(id)) {}
  uint32_t now() const { return app_.now(); }
  void beep(uint16_t hz, uint16_t ms) { app_.beep(hz, ms); }
  // key=value saves in sd:/games/save/<id>.ini (flash when there is no card)
  bool save(const std::string& key, const std::string& value);
  std::string load(const std::string& key, const std::string& fallback = "");
  void exit() { exitRequested_ = true; }
  uint32_t random();
  bool exitRequested() const { return exitRequested_; }
  void clearExit() { exitRequested_ = false; }

 private:
  app::App& app_;
  std::string id_;
  bool exitRequested_ = false;
  uint32_t seed_ = 12345;
};

class CanvasApp {
 public:
  virtual ~CanvasApp() = default;
  virtual void onStart(Host&) {}
  virtual void onInput(Host&, const app::InputEvent&) {}
  virtual void onTick(Host&) {}
  virtual void draw(Host&, ui::Framebuffer& fb) = 0;
  virtual void onExit(Host&) {}         // save here; keep it short (kExitBudgetMs)
  virtual void onSaveRequest(Host&) {}  // battery low
  // level-1 / level-2 hooks are not virtual here: builtin<T>() binds them, so a missing one is a build error
};

struct AppEntry {
  std::string id, label;
  Bypass level = Bypass::Default;
  AppType type = AppType::Canvas;
  std::function<std::unique_ptr<CanvasApp>()> make;
  std::function<void(CanvasApp&, Host&)> pause;                           // level 1
  std::function<void(CanvasApp&, Host&, const app::RawEvent&)> raw;       // level 2
  std::string packDir;  // SD packs: sd:/games/<dir>
};

// detection of the level-1 / level-2 callbacks
template <typename T, typename = void>
struct HasPause : std::false_type {};
template <typename T>
struct HasPause<T, std::void_t<decltype(std::declval<T&>().onPauseRequest(std::declval<Host&>()))>> : std::true_type {};
template <typename T, typename = void>
struct HasRaw : std::false_type {};
template <typename T>
struct HasRaw<T, std::void_t<decltype(std::declval<T&>().onRawInput(std::declval<Host&>(), std::declval<const app::RawEvent&>()))>>
    : std::true_type {};

template <typename T>
AppEntry builtin(const char* label) {
  constexpr Bypass level = bypassOf(T::kId);
  static_assert(level != Bypass::CustomPause || HasPause<T>::value, "bypass level 1 needs onPauseRequest(Host&)");
  static_assert(level != Bypass::RawInput || HasRaw<T>::value, "bypass level 2 needs onRawInput(Host&, const RawEvent&)");
  AppEntry e;
  e.id = T::kId;
  e.label = label;
  e.level = level;
  e.make = [] { return std::unique_ptr<CanvasApp>(new T()); };
  if constexpr (HasPause<T>::value) e.pause = [](CanvasApp& a, Host& h) { static_cast<T&>(a).onPauseRequest(h); };
  if constexpr (HasRaw<T>::value)
    e.raw = [](CanvasApp& a, Host& h, const app::RawEvent& r) { static_cast<T&>(a).onRawInput(h, r); };
  return e;
}

// ---- SD packs ----
struct PackInfo {
  std::string dir, name;
  AppType type = AppType::Canvas;
  std::string engine;  // sprite2d | menu_flow
  std::vector<std::string> pages;  // menu_flow: "TITLE|line|line"
  std::string image;   // sprite2d: the scene picture (.c16)
};
// Checks a pack the way the engine loads it. False with a short reason (shown on G2f).
bool validatePack(app::App& app, const std::string& dir, PackInfo& out, std::string& reason);
std::vector<std::string> packDirs(app::App& app);
// The manifest's name= (the folder name when missing).
std::string packName(app::App& app, const std::string& dir);

// The host screen (G3) for a canvas app.
std::unique_ptr<app::Screen> makeHostScreen(app::App& app, const AppEntry& e);

}  // namespace apps

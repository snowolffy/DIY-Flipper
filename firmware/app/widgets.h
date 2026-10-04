// widgets.h - the screen templates the module flows are built from: catalog list, confirm dialog, popup
// menu, message / result, busy, key-value detail, text input carousel, digit entry and toast. Each takes
// its mockup code and is configured with callbacks, so a flow node is usually one constructor call.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "app/app.h"
#include "app/toolkit.h"

namespace app {

using Action = std::function<void(App&)>;

// Common base: code + menu path name.
class BasicScreen : public Screen {
 public:
  BasicScreen(std::string code, std::string path) : code_(std::move(code)), path_(std::move(path)) {}
  const char* code() const override { return code_.c_str(); }
  std::string title() const override { return path_; }

 protected:
  std::string code_, path_;
};

// ---- catalog list ----
// < > move (wrapping, skipping info rows), OK picks, Cancel goes back, Cancel hold manages the row.
class ListScreen : public BasicScreen {
 public:
  ListScreen(std::string code, std::string title, std::string path = "")
      : BasicScreen(std::move(code), path.empty() ? title : path), titleText_(std::move(title)) {}
  InputMode inputMode() const override { return InputMode::List; }
  void onEnter(App& app) override { reload(app); }
  void onResume(App& app) override { reload(app); }
  void onInput(App& app, const InputEvent& e) override;
  void draw(App& app, ui::Framebuffer& fb) override;

  int selected() const { return sel_; }
  void select(App& app, int index);
  void reload(App& app);
  const std::vector<tk::Row>& rowsShown() const { return rows_; }

 protected:
  virtual std::vector<tk::Row> rows(App& app) = 0;
  virtual void ok(App&, int) {}
  virtual void manage(App&, int) {}  // Cancel hold on a row
  virtual bool canManage(App&, int) const { return false; }
  virtual void back(App& app) { app.pop(); }
  virtual std::string bottom(App& app) const;
  virtual std::string emptyText() const { return "EMPTY"; }
  virtual bool editing() const { return false; }
  virtual void onEditInput(App&, const InputEvent&) {}

  std::string titleText_;
  std::vector<tk::Row> rows_;
  int sel_ = 0, first_ = 0;
  uint32_t selAt_ = 0;
};

// A list built from items with callbacks: the usual module menu.
struct MenuItem {
  tk::Row row;
  Action onOk;           // null = nothing
  Action onManage;       // Cancel hold; null = nothing
};
class MenuScreen : public ListScreen {
 public:
  using Builder = std::function<std::vector<MenuItem>(App&)>;
  MenuScreen(std::string code, std::string title, Builder b, std::string path = "")
      : ListScreen(std::move(code), std::move(title), std::move(path)), build_(std::move(b)) {}
  MenuScreen& empty(std::string t) { empty_ = std::move(t); return *this; }
  MenuScreen& onBack(Action a) { back_ = std::move(a); return *this; }
  MenuScreen& bottomText(std::function<std::string(App&)> f) { bottom_ = std::move(f); return *this; }

 protected:
  std::vector<tk::Row> rows(App& app) override;
  void ok(App& app, int i) override;
  void manage(App& app, int i) override;
  bool canManage(App&, int i) const override { return i < (int)items_.size() && items_[i].onManage; }
  void back(App& app) override { back_ ? back_(app) : app.pop(); }
  std::string bottom(App& app) const override { return bottom_ ? bottom_(app) : ListScreen::bottom(app); }
  std::string emptyText() const override { return empty_; }

  Builder build_;
  std::vector<MenuItem> items_;
  std::string empty_ = "EMPTY";
  Action back_;
  std::function<std::string(App&)> bottom_;
};

// ---- confirm dialog (overlay) ----
class DialogScreen : public BasicScreen {
 public:
  DialogScreen(std::string code, std::vector<std::string> lines, Action yes, Action no = nullptr,
               std::string hint = "OK=YES  CANCEL=NO")
      : BasicScreen(std::move(code), ""), lines_(std::move(lines)), hint_(std::move(hint)),
        yes_(std::move(yes)), no_(std::move(no)) {}
  bool overlay() const override { return true; }
  void onInput(App& app, const InputEvent& e) override;
  void draw(App&, ui::Framebuffer& fb) override { tk::dialog(fb, lines_, hint_); }

 private:
  std::vector<std::string> lines_;
  std::string hint_;
  Action yes_, no_;
};

// ---- popup menu (overlay) ----
class PopupScreen : public BasicScreen {
 public:
  PopupScreen(std::string code, std::vector<std::string> items, std::function<void(App&, int)> pick,
              Action cancel = nullptr)
      : BasicScreen(std::move(code), ""), items_(std::move(items)), pick_(std::move(pick)), cancel_(std::move(cancel)) {}
  bool overlay() const override { return true; }
  InputMode inputMode() const override { return InputMode::List; }
  uint8_t deferMask() const override { return 0; }
  void onInput(App& app, const InputEvent& e) override;
  void draw(App&, ui::Framebuffer& fb) override { tk::popup(fb, items_, sel_, cy_); }
  PopupScreen& centreY(int16_t cy) { cy_ = cy; return *this; }

 private:
  std::vector<std::string> items_;
  std::function<void(App&, int)> pick_;
  Action cancel_;
  int sel_ = 0;
  int16_t cy_ = 0;
};

// ---- message / result / busy / detail ----
// One configurable "normal" page: title bar (optional), a body (icon + lines, busy animation, key/values,
// progress, or custom draw), bottom hint, OK / Cancel actions and an optional timer.
class PageScreen : public BasicScreen {
 public:
  PageScreen(std::string code, std::string title, std::string path = "")
      : BasicScreen(std::move(code), path.empty() ? title : path), titleText_(std::move(title)) {}

  PageScreen& icon(const char* key) { icon_ = key; return *this; }
  PageScreen& lines(std::vector<std::string> l) { lines_ = std::move(l); return *this; }
  PageScreen& linesY(int16_t y, int16_t step = 10) { linesY_ = y; linesStep_ = step; return *this; }
  PageScreen& busy() { busy_ = true; return *this; }
  PageScreen& kv(std::function<std::vector<tk::KV>(App&)> f) { kv_ = std::move(f); return *this; }
  PageScreen& hint(std::string h) { hint_ = std::move(h); return *this; }
  PageScreen& noBottom() { bottomRule_ = false; return *this; }
  PageScreen& noStatus() { status_ = false; return *this; }
  PageScreen& onOk(Action a) { ok_ = std::move(a); return *this; }
  PageScreen& onCancel(Action a) { cancel_ = std::move(a); return *this; }
  PageScreen& timer(uint32_t ms, Action a) { timerMs_ = ms; timer_ = std::move(a); return *this; }
  PageScreen& tick(std::function<void(App&, PageScreen&)> f) { tick_ = std::move(f); return *this; }
  PageScreen& system(std::function<void(App&, SysEvent)> f) { sys_ = std::move(f); return *this; }
  PageScreen& custom(std::function<void(App&, ui::Framebuffer&)> f) { custom_ = std::move(f); return *this; }
  PageScreen& awake() { awake_ = true; return *this; }
  PageScreen& defer(uint8_t m) { defer_ = m; return *this; }
  PageScreen& input(std::function<bool(App&, const InputEvent&)> f) { input_ = std::move(f); return *this; }

  bool statusBar() const override { return status_; }
  bool keepAwake() const override { return awake_; }
  uint8_t deferMask() const override { return defer_; }
  void onEnter(App& app) override { enteredAt_ = app.now(); }
  void onInput(App& app, const InputEvent& e) override;
  void onSystem(App& app, SysEvent e) override { if (sys_) sys_(app, e); }
  void onTick(App& app) override;
  void draw(App& app, ui::Framebuffer& fb) override;

  uint32_t enteredAt() const { return enteredAt_; }
  void restartTimer(App& app) { enteredAt_ = app.now(); timerFired_ = false; }
  std::vector<std::string>& textLines() { return lines_; }
  std::string& hintText() { return hint_; }

 private:
  std::string titleText_;
  const char* icon_ = nullptr;
  std::vector<std::string> lines_;
  int16_t linesY_ = -1, linesStep_ = 10;
  bool busy_ = false, bottomRule_ = true, status_ = true, awake_ = false, timerFired_ = false;
  std::function<std::vector<tk::KV>(App&)> kv_;
  std::string hint_;
  Action ok_, cancel_, timer_;
  uint32_t timerMs_ = 0, enteredAt_ = 0;
  uint8_t defer_ = 0;
  std::function<void(App&, PageScreen&)> tick_;
  std::function<void(App&, SysEvent)> sys_;
  std::function<void(App&, ui::Framebuffer&)> custom_;
  std::function<bool(App&, const InputEvent&)> input_;
};

// ---- text input (carousel) ----
// < > turn, OK adds, Cancel deletes the last character (on an empty field: back), Cancel hold cancels,
// OK hold switches case (charsets with lower case). The carousel ends with a delete slot and a done slot.
class TextInputScreen : public BasicScreen {
 public:
  static const char* const kFileChars;      // A-Z 0-9 _ - .
  static const char* const kPasswordChars;  // A-Z a-z 0-9 _ - . ! @ # $ % ^ & * ( )  (+ space for messages)
  using Done = std::function<void(App&, const std::string&)>;
  // check returns an error to show (and keep editing), or "" when the text is fine
  using Check = std::function<std::string(App&, const std::string&)>;

  TextInputScreen(std::string code, std::string title, std::string charset, std::string text, size_t maxLen,
                  Done done, Action cancel = nullptr);
  TextInputScreen& check(Check c) { check_ = std::move(c); return *this; }
  TextInputScreen& allowEmpty() { allowEmpty_ = true; return *this; }
  // shows ABC / abc above the field (WiFi password)
  TextInputScreen& showCase() { showCase_ = true; return *this; }
  InputMode inputMode() const override { return InputMode::Text; }
  void onInput(App& app, const InputEvent& e) override;
  void draw(App& app, ui::Framebuffer& fb) override;
  const std::string& text() const { return text_; }

 private:
  int slots() const { return (int)chars_.size() + 2; }
  std::string titleText_, chars_, text_;
  size_t max_;
  Done done_;
  Action cancel_;
  Check check_;
  int pick_ = 0;
  bool lower_ = false, hasCase_ = false, allowEmpty_ = false, showCase_ = false;
};

// ---- digit entry (PIN / emergency code) ----
// < > turn the digit, OK confirms it, Cancel deletes the last one, Cancel hold leaves. The last OK calls
// done with all digits.
class DigitEntryScreen : public BasicScreen {
 public:
  using Done = std::function<void(App&, const std::string&)>;
  DigitEntryScreen(std::string code, std::string title, int digits, Done done, Action leave)
      : BasicScreen(std::move(code), ""), titleText_(std::move(title)), n_(digits), done_(std::move(done)),
        leave_(std::move(leave)) {}
  bool statusBar() const override { return false; }
  uint8_t deferMask() const override { return bit(hal::Button::Cancel); }
  void onInput(App& app, const InputEvent& e) override;
  void draw(App& app, ui::Framebuffer& fb) override;
  void clear() { digits_.clear(); }

 private:
  std::string titleText_;
  int n_;
  Done done_;
  Action leave_;
  std::string digits_;
  int pick_ = 0;
};

// ---- toast that is a flow state (IR S2t "Sent") ----
class ToastScreen : public BasicScreen {
 public:
  ToastScreen(std::string code, std::string text, uint32_t ms)
      : BasicScreen(std::move(code), ""), text_(std::move(text)), ms_(ms) {}
  bool overlay() const override { return true; }
  bool dimBelow() const override { return false; }
  void onEnter(App& app) override { at_ = app.now(); }
  void onInput(App& app, const InputEvent& e) override;
  void onTick(App& app) override {
    if (app.now() - at_ >= ms_) app.pop();
  }
  void draw(App&, ui::Framebuffer& fb) override { tk::toast(fb, text_); }

 private:
  std::string text_;
  uint32_t ms_, at_ = 0;
};

}  // namespace app

// valuelist.h - a catalog list whose rows hold values (Settings): toggles flip on OK, numbers edit in the
// row (< > change, hold to speed up, OK saves, Cancel puts the old value back), actions open a page.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "app/widgets.h"

namespace app {

struct ValueRow {
  enum Kind : uint8_t { Button, Toggle, Number, Info };
  std::string label;
  Kind kind = Button;
  std::function<std::string(App&)> value;  // right-aligned text
  app::Action onOk;                        // Button / Toggle
  // Number: index into steps
  std::vector<int> steps;
  std::function<int(App&)> get;
  std::function<void(App&, int)> set;      // called on every change (live), and on cancel with the old value
  std::function<std::string(int)> format;
  std::function<bool(App&)> visible;       // hidden rows (Sleep after when Never)
};

class ValueListScreen : public ListScreen {
 public:
  using Builder = std::function<std::vector<ValueRow>(App&)>;
  // editCode: the code while a number is being edited (T1e), or empty to keep the list's code
  ValueListScreen(std::string code, std::string editCode, std::string title, Builder b, std::string path = "")
      : ListScreen(std::move(code), std::move(title), std::move(path)), editCode_(std::move(editCode)), build_(std::move(b)) {}
  const char* code() const override { return editing_ && !editCode_.empty() ? editCode_.c_str() : code_.c_str(); }
  // while editing, OK/Cancel act on press like the flow's T1e page
  uint8_t deferMask() const override { return editing_ ? 0 : ListScreen::deferMask(); }

 protected:
  std::vector<tk::Row> rows(App& app) override;
  void ok(App& app, int i) override;
  bool editing() const override { return editing_; }
  void onEditInput(App& app, const InputEvent& e) override;

  std::string editCode_;
  Builder build_;
  std::vector<ValueRow> items_;
  bool editing_ = false;
  int editIdx_ = 0, editVal_ = 0, original_ = 0;
};

}  // namespace app

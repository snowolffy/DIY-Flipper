#include "app/valuelist.h"

namespace app {

std::vector<tk::Row> ValueListScreen::rows(App& app) {
  items_.clear();
  for (auto& r : build_(app))
    if (!r.visible || r.visible(app)) items_.push_back(std::move(r));
  std::vector<tk::Row> out;
  for (const auto& r : items_) {
    tk::Row row;
    row.label = r.label;
    if (r.kind == ValueRow::Number && r.get) row.value = r.format ? r.format(r.get(app)) : std::to_string(r.get(app));
    else if (r.value) row.value = r.value(app);
    row.info = r.kind == ValueRow::Info;
    row.valueWhite = true;
    out.push_back(row);
  }
  return out;
}

void ValueListScreen::ok(App& app, int i) {
  ValueRow& r = items_[i];
  if (r.kind == ValueRow::Number) {
    editing_ = true;
    editIdx_ = i;
    original_ = editVal_ = r.get(app);
    return;
  }
  if (r.onOk) {
    Action a = r.onOk;
    a(app);
    if (r.kind == ValueRow::Toggle) {
      app.saveSettings();
      reload(app);
    }
  }
}

void ValueListScreen::onEditInput(App& app, const InputEvent& e) {
  ValueRow& r = items_[editIdx_];
  const std::vector<int>& st = r.steps;
  int pos = 0;
  for (size_t k = 0; k < st.size(); k++)
    if (st[k] == editVal_) pos = (int)k;
  if (e.step(hal::Button::Left) || e.step(hal::Button::Right)) {
    pos += e.button == hal::Button::Left ? -1 : 1;
    if (pos < 0) pos = 0;
    if (pos >= (int)st.size()) pos = (int)st.size() - 1;
    editVal_ = st[pos];
    r.set(app, editVal_);  // live (brightness changes the backlight at once)
  } else if (e.tap(hal::Button::Ok)) {
    editing_ = false;
    app.saveSettings();
  } else if (e.tap(hal::Button::Cancel)) {
    editing_ = false;
    r.set(app, original_);
  } else {
    return;
  }
  reload(app);
}

}  // namespace app

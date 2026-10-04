#include "app/widgets.h"

#include <cctype>
#include <cstring>

#include "app/theme.h"
#include "ui/gfx.h"

namespace app {

using hal::Button;
namespace col = ui::color;

// ---------------- list ----------------

void ListScreen::reload(App& app) {
  rows_ = rows(app);
  if (rows_.empty()) {
    sel_ = first_ = 0;
    return;
  }
  if (sel_ >= (int)rows_.size()) sel_ = (int)rows_.size() - 1;
  if (sel_ < 0) sel_ = 0;
  // never rest on an info row
  for (int k = 0; k < (int)rows_.size() && rows_[sel_].info; k++) sel_ = (sel_ + 1) % (int)rows_.size();
  select(app, sel_);
}

void ListScreen::select(App& app, int index) {
  if (index != sel_) selAt_ = app.now();
  sel_ = index;
  if (sel_ < first_) first_ = sel_;
  if (sel_ >= first_ + ui::kRowsVisible) first_ = sel_ - ui::kRowsVisible + 1;
  // keep a leading info row (status line) visible when the list fits
  if ((int)rows_.size() <= ui::kRowsVisible) first_ = 0;
}

void ListScreen::onInput(App& app, const InputEvent& e) {
  if (editing()) {
    onEditInput(app, e);
    return;
  }
  const int n = (int)rows_.size();
  if (n && (e.step(Button::Left) || e.step(Button::Right))) {
    const int d = e.button == Button::Left ? -1 : 1;
    int i = sel_;
    for (int k = 0; k < n; k++) {
      i = (i + d + n) % n;
      if (!rows_[i].info) break;
    }
    select(app, i);
  } else if (e.tap(Button::Ok)) {
    if (n && !rows_[sel_].info && !rows_[sel_].disabled) ok(app, sel_);
  } else if (e.tap(Button::Cancel)) {
    back(app);
  } else if (e.hold(Button::Cancel)) {
    if (n && canManage(app, sel_)) manage(app, sel_);
  }
}

std::string ListScreen::bottom(App&) const {
  int total = 0, pos = 0;
  for (int i = 0; i < (int)rows_.size(); i++) {
    if (rows_[i].info) continue;
    total++;
    if (i <= sel_) pos++;
  }
  if (!total) return "";
  return std::to_string(pos) + "/" + std::to_string(total);
}

void ListScreen::draw(App& app, ui::Framebuffer& fb) {
  tk::titleBar(fb, titleText_);
  if (rows_.empty()) tk::emptyText(fb, emptyText());
  else tk::catalogList(fb, rows_, sel_, first_, app.now() - selAt_, editing());
  tk::bottomBar(fb, bottom(app));
}

std::vector<tk::Row> MenuScreen::rows(App& app) {
  items_ = build_(app);
  std::vector<tk::Row> r;
  r.reserve(items_.size());
  for (const auto& it : items_) r.push_back(it.row);
  return r;
}

void MenuScreen::ok(App& app, int i) {
  if (i < (int)items_.size() && items_[i].onOk) {
    Action a = items_[i].onOk;  // the item list may be rebuilt by the action
    a(app);
    reload(app);
  }
}

void MenuScreen::manage(App& app, int i) {
  if (i < (int)items_.size() && items_[i].onManage) {
    Action a = items_[i].onManage;
    a(app);
  }
}

// ---------------- dialog / popup / toast ----------------

void DialogScreen::onInput(App& app, const InputEvent& e) {
  if (e.tap(Button::Ok)) {
    Action a = yes_;
    if (a) a(app);
    else app.pop();
  } else if (e.tap(Button::Cancel)) {
    Action a = no_;
    if (a) a(app);
    else app.pop();
  }
}

void PopupScreen::onInput(App& app, const InputEvent& e) {
  const int n = (int)items_.size();
  if (e.step(Button::Left)) sel_ = (sel_ + n - 1) % n;
  else if (e.step(Button::Right)) sel_ = (sel_ + 1) % n;
  else if (e.tap(Button::Ok)) {
    auto p = pick_;
    p(app, sel_);
  } else if (e.tap(Button::Cancel)) {
    if (cancel_) {
      Action a = cancel_;
      a(app);
    } else {
      app.pop();
    }
  }
}

void ToastScreen::onInput(App& app, const InputEvent& e) {
  if (e.tap(Button::Cancel)) app.pop();
}

// ---------------- page ----------------

void PageScreen::onInput(App& app, const InputEvent& e) {
  if (input_ && input_(app, e)) return;
  if (e.tap(Button::Ok) && ok_) {
    Action a = ok_;
    a(app);
  } else if (e.tap(Button::Cancel) && cancel_) {
    Action a = cancel_;
    a(app);
  }
}

void PageScreen::onTick(App& app) {
  if (tick_) tick_(app, *this);
  if (timer_ && !timerFired_ && app.now() - enteredAt_ >= timerMs_) {
    timerFired_ = true;
    Action a = timer_;
    a(app);
  }
}

void PageScreen::draw(App& app, ui::Framebuffer& fb) {
  if (!titleText_.empty()) tk::titleBar(fb, titleText_);
  const uint32_t ms = app.now() - enteredAt_;
  if (kv_) tk::keyValues(fb, kv_(app));
  if (busy_) tk::busy(fb, icon_, lines_, ms);
  else if (!lines_.empty() || icon_) {
    if (linesY_ >= 0) {
      if (icon_) ui::drawPic(fb, 52, (int16_t)(linesY_ - 39), theme::icon(icon_), 3);
      tk::centredLines(fb, linesY_, lines_, false, linesStep_);
    } else {
      tk::message(fb, icon_, lines_);
    }
  }
  if (custom_) custom_(app, fb);
  if (bottomRule_) tk::bottomBar(fb, hint_);
}

// ---------------- text input ----------------

const char* const TextInputScreen::kFileChars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.";
const char* const TextInputScreen::kPasswordChars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.!@#$%^&*()";

TextInputScreen::TextInputScreen(std::string code, std::string title, std::string charset, std::string text,
                                 size_t maxLen, Done done, Action cancel)
    : BasicScreen(std::move(code), ""), titleText_(std::move(title)), chars_(std::move(charset)),
      text_(std::move(text)), max_(maxLen), done_(std::move(done)), cancel_(std::move(cancel)) {
  // a charset with letters gets the case switch when it is used for passwords/messages (has symbols)
  hasCase_ = chars_.find('!') != std::string::npos || chars_.find(' ') != std::string::npos;
}

void TextInputScreen::onInput(App& app, const InputEvent& e) {
  const int n = slots();
  if (e.step(Button::Left)) pick_ = (pick_ + n - 1) % n;
  else if (e.step(Button::Right)) pick_ = (pick_ + 1) % n;
  else if (e.hold(Button::Ok)) {
    if (hasCase_) lower_ = !lower_;
  } else if (e.tap(Button::Ok)) {
    if (pick_ == n - 2) {  // delete slot
      if (!text_.empty()) text_.pop_back();
    } else if (pick_ == n - 1) {  // done slot
      if (text_.empty() && !allowEmpty_) return;
      if (check_) {
        const std::string err = check_(app, text_);
        if (!err.empty()) {
          app.toast(err);
          return;
        }
      }
      Done d = done_;
      d(app, text_);
    } else if (text_.size() < max_) {
      char c = chars_[pick_];
      if (lower_) c = (char)std::tolower((unsigned char)c);
      text_ += c;
    }
  } else if (e.tap(Button::Cancel)) {
    if (!text_.empty()) text_.pop_back();
    else if (cancel_) cancel_(app);
    else app.pop();
  } else if (e.hold(Button::Cancel)) {
    if (cancel_) {
      Action a = cancel_;
      a(app);
    } else {
      app.pop();
    }
  }
}

void TextInputScreen::draw(App&, ui::Framebuffer& fb) {
  tk::titleBar(fb, titleText_);
  if (hasCase_ && showCase_) ui::drawTextRight(fb, theme::small(), 122, 25, lower_ ? "abc" : "ABC", col::kGray);
  tk::textField(fb, text_);
  std::vector<std::string> labels;
  std::vector<const char*> icons((size_t)slots(), nullptr);
  for (char c : chars_) {
    const char s[2] = {lower_ ? (char)std::tolower((unsigned char)c) : c, 0};
    labels.emplace_back(s);
  }
  labels.emplace_back("");
  labels.emplace_back("");
  icons[(size_t)slots() - 2] = "backspace_new";
  icons[(size_t)slots() - 1] = "check_new";
  tk::carousel(fb, 74, 26, slots(), pick_, labels, icons);
  ui::drawTextCentered(fb, theme::small(), 112, "<> PICK", col::kGray);
  tk::bottomBar(fb, "OK=ADD  CANCEL=DEL");
}

// ---------------- digit entry ----------------

void DigitEntryScreen::onInput(App& app, const InputEvent& e) {
  if (e.step(Button::Left)) pick_ = (pick_ + 9) % 10;
  else if (e.step(Button::Right)) pick_ = (pick_ + 1) % 10;
  else if (e.tap(Button::Ok)) {
    digits_ += (char)('0' + pick_);
    if ((int)digits_.size() == n_) {
      const std::string d = digits_;
      digits_.clear();
      Done f = done_;
      f(app, d);
    }
  } else if (e.tap(Button::Cancel)) {
    if (!digits_.empty()) digits_.pop_back();
  } else if (e.hold(Button::Cancel)) {
    Action a = leave_;
    if (a) a(app);
    else app.pop();
  }
}

void DigitEntryScreen::draw(App&, ui::Framebuffer& fb) {
  ui::drawTextCentered(fb, theme::small(), 22, titleText_.c_str(), col::kWhite);
  tk::digitBoxes(fb, n_, (int)digits_.size(), (char)('0' + pick_));
  std::vector<std::string> labels;
  for (int i = 0; i < 10; i++) labels.push_back(std::string(1, (char)('0' + i)));
  tk::carousel(fb, 84, 22, 10, pick_, labels, {});
  tk::bottomBar(fb, "OK=NEXT  CANCEL=DEL");
}

}  // namespace app

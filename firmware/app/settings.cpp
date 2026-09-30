#include "app/settings.h"

#include <string>

namespace app {

namespace {

// tolerate files edited on a PC: CRLF line endings, spaces around keys and values
std::string trim(const std::string& s) {
  size_t b = 0, e = s.size();
  while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r')) b++;
  while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) e--;
  return s.substr(b, e - b);
}

}  // namespace

void Settings::load(const hal::Storage& storage) {
  std::string text;
  if (!storage.read(hal::Volume::Flash, kPath, text)) return;
  size_t pos = 0;
  while (pos < text.size()) {
    size_t end = text.find('\n', pos);
    if (end == std::string::npos) end = text.size();
    const std::string line = text.substr(pos, end - pos);
    pos = end + 1;
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = trim(line.substr(0, eq));
    const std::string value = trim(line.substr(eq + 1));
    if (key == "invert") invert = value == "1";
    else if (key == "theme") theme = value;
  }
}

bool Settings::save(hal::Storage& storage) const {
  std::string text = "invert=";
  text += invert ? "1" : "0";
  text += "\n";
  if (!theme.empty()) text += "theme=" + theme + "\n";
  return storage.write(hal::Volume::Flash, kPath, text);
}

}  // namespace app

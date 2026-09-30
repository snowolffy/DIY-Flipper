#include "app/settings.h"

#include <string>

namespace app {

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
    const std::string key = line.substr(0, eq);
    const std::string value = line.substr(eq + 1);
    if (key == "invert") invert = value == "1";
  }
}

bool Settings::save(hal::Storage& storage) const {
  std::string text = "invert=";
  text += invert ? "1" : "0";
  text += "\n";
  return storage.write(hal::Volume::Flash, kPath, text);
}

}  // namespace app

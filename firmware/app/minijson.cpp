#include "app/minijson.h"

namespace minijson {

std::string escape(const std::string& s) {
  std::string out;
  for (char c : s) {
    if (c == '"' || c == '\\') out += '\\';
    if (c == '\n') {
      out += "\\n";
      continue;
    }
    out += c;
  }
  return out;
}

namespace {

// Position just after `"key":` (and any spaces), or npos.
size_t valueStart(const std::string& json, const std::string& key) {
  const std::string quoted = "\"" + key + "\"";
  size_t p = json.find(quoted);
  if (p == std::string::npos) return p;
  p = json.find(':', p + quoted.size());
  if (p == std::string::npos) return p;
  p++;
  while (p < json.size() && (json[p] == ' ' || json[p] == '\t' || json[p] == '\n' || json[p] == '\r')) p++;
  return p;
}

// Reads a string starting at the opening quote at json[p]; returns the position after the closing quote.
size_t readString(const std::string& json, size_t p, std::string& out) {
  out.clear();
  for (p++; p < json.size() && json[p] != '"'; p++) {
    if (json[p] == '\\' && p + 1 < json.size()) {
      p++;
      out += json[p] == 'n' ? '\n' : json[p];
    } else {
      out += json[p];
    }
  }
  return p + 1;
}

}  // namespace

std::string field(const std::string& json, const std::string& key) {
  size_t p = valueStart(json, key);
  if (p == std::string::npos || p >= json.size()) return {};
  std::string out;
  if (json[p] == '"') {
    readString(json, p, out);
    return out;
  }
  while (p < json.size() && json[p] != ',' && json[p] != '}' && json[p] != ' ' && json[p] != '\n' && json[p] != '\r')
    out += json[p++];
  return out;
}

std::vector<std::string> stringArray(const std::string& json, const std::string& key) {
  std::vector<std::string> out;
  size_t p = valueStart(json, key);
  if (p == std::string::npos || p >= json.size() || json[p] != '[') return out;
  for (p++; p < json.size() && json[p] != ']'; p++) {
    if (json[p] == '"') {
      std::string s;
      p = readString(json, p, s) - 1;
      out.push_back(s);
    }
  }
  return out;
}

}  // namespace minijson

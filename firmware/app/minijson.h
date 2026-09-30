// minijson.h - just enough JSON for the small flat files the firmware writes itself (saved IR remotes,
// NFC dumps). Not a general parser: one object, string / number / array-of-string values, no nesting.
#pragma once

#include <string>
#include <vector>

namespace minijson {

std::string escape(const std::string& s);

// Value of "key" as written: a string's content (unescaped), or a number's text. Empty if missing.
std::string field(const std::string& json, const std::string& key);
// String entries of "key": [ "a", "b" ]. Empty if missing.
std::vector<std::string> stringArray(const std::string& json, const std::string& key);

}  // namespace minijson

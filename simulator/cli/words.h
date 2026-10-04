// words.h - the one vocabulary: terminal words <-> command objects (core/commands.h). The window's command
// log prints commands in these words and `sim set ...` parses them, so a log line can be typed back as is.
#pragma once

#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace simcli {

using json = nlohmann::json;

// "press OK", "hold OK 800", "set battery 80", "set nfc place 04A23B1C ..." for a command object.
std::string describe(const json& cmd);
// Words after `sim` (press OK / set battery 80 / ...) -> command object. False with a message for a typo.
bool parseWords(const std::vector<std::string>& words, json& cmd, std::string& err);
// Help text for `sim set --help`.
const char* setHelp();

}  // namespace simcli

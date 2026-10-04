// cli.h - the `sim` command line: one exe for the window, the terminal and CI.
#pragma once

#include <string>
#include <vector>

namespace simcli {

int cmdRun(const std::vector<std::string>& args);
int cmdSession(const std::string& cmd, const std::vector<std::string>& args);  // open/close/press/... (session.cpp)
int cmdServe(const std::vector<std::string>& args);                          // internal: the session process

}  // namespace simcli

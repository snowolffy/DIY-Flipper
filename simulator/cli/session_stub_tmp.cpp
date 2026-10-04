#include <iostream>
#include "cli/cli.h"
namespace simcli {
int cmdSession(const std::string& cmd, const std::vector<std::string>&) { std::cerr << "not yet: " << cmd << "\n"; return 2; }
int cmdServe(const std::vector<std::string>&) { return 2; }
}

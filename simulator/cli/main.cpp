// sim - DIY Flipper emulator. One portable exe: the window (a local web page served by this process), the
// terminal commands that drive the same session, and the headless script runner for CI.
//
//   sim gui [--project DIR]        start a session and open its window
//   sim open [--project DIR]       start a session in the background (no window, virtual time)
//   sim close                      end the session
//   sim press|hold|down|up BTN     buttons (OK, CANCEL, LEFT, RIGHT, POWER); hold BTN [ms]
//   sim wait MS                    advance virtual time
//   sim state [--json]             menu path, screen, every mock, power, display hash
//   sim shot FILE.png [--scale N]  the device screen
//   sim shot-ui FILE.png           the whole window (needs Chrome/Chromium/Edge)
//   sim set WHAT ...               change a mock (see `sim set --help`)
//   sim import FILE                Import Asset
//   sim run SCRIPT|DIR ...         run scripts headless (fresh storage copy per script)
//   sim restart [--cold|--wake]    power-cycle the device
#include <iostream>
#include <string>
#include <vector>

#include "cli/cli.h"

int main(int argc, char** argv) {
  std::vector<std::string> args(argv + 1, argv + argc);
  if (args.empty() || args[0] == "-h" || args[0] == "--help" || args[0] == "help") {
    std::cout << "sim - DIY Flipper emulator\n\n"
                 "  sim gui [--project DIR]        start a session and open its window\n"
                 "  sim open [--project DIR]       start a session in the background (virtual time)\n"
                 "  sim close                      end the session\n"
                 "  sim press|down|up BTN          OK CANCEL LEFT RIGHT POWER\n"
                 "  sim hold BTN [MS]              hold a button (default 800 ms)\n"
                 "  sim wait MS                    advance virtual time\n"
                 "  sim state [--json]             what the device and every mock are doing\n"
                 "  sim shot FILE.png [--scale N]  screenshot of the device screen\n"
                 "  sim shot-ui FILE.png           screenshot of the whole window\n"
                 "  sim set WHAT ...               change a mock: sim set --help\n"
                 "  sim cmd JSON                   send one raw command object\n"
                 "  sim import FILE                Import Asset (.c16 .b1i .b1f theme .zip)\n"
                 "  sim restart [--cold|--wake]    power-cycle (default cold)\n"
                 "  sim run SCRIPT|DIR ...         run scripts headless\n";
    return args.empty() ? 2 : 0;
  }
  const std::string cmd = args[0];
  args.erase(args.begin());
  if (cmd == "run") return simcli::cmdRun(args);
  if (cmd == "serve") return simcli::cmdServe(args);
  return simcli::cmdSession(cmd, args);
}

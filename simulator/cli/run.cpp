#include "cli/cli.h"

#include <algorithm>
#include <iostream>

#include "core/script.h"

namespace fs = std::filesystem;

namespace simcli {

// sim run [--project DIR] [--shot-final FILE.png] [--keep-storage] script.json|scripts-dir ...
int cmdRun(const std::vector<std::string>& args) {
  sim::RunOptions opt;
  bool projectGiven = false;
  std::vector<fs::path> scripts;
  for (size_t i = 0; i < args.size(); i++) {
    const std::string& a = args[i];
    if (a == "--project" && i + 1 < args.size()) {
      opt.projectDir = args[++i];
      projectGiven = true;
    } else if (a == "--shot-final" && i + 1 < args.size()) {
      opt.shotFinal = args[++i];
    } else if (a == "--keep-storage") {
      opt.keepStorage = true;
    } else if (!a.empty() && a[0] == '-') {
      std::cerr << "unknown option " << a << "\n";
      return 2;
    } else if (fs::is_directory(a)) {
      std::vector<fs::path> found;
      for (const auto& e : fs::directory_iterator(a))
        if (e.path().extension() == ".json") found.push_back(e.path());
      std::sort(found.begin(), found.end());
      scripts.insert(scripts.end(), found.begin(), found.end());
    } else {
      scripts.push_back(a);
    }
  }
  if (scripts.empty()) {
    std::cerr << "usage: sim run [--project DIR] [--shot-final FILE.png] [--keep-storage] script.json|dir ...\n";
    return 2;
  }
  if (!opt.shotFinal.empty() && scripts.size() > 1) {
    std::cerr << "--shot-final needs exactly one script\n";
    return 2;
  }
  int failed = 0;
  for (const fs::path& script : scripts) {
    sim::RunOptions o = opt;
    // scripts live in <project>/scripts/, so the project is the script folder's parent
    if (!projectGiven) o.projectDir = fs::absolute(script).parent_path().parent_path();
    const sim::ScriptResult r = sim::runScript(script, o);
    if (!r.loaded) {
      std::cout << "ERROR " << r.name << ": " << r.error << "\n";
      failed++;
      continue;
    }
    for (const auto& a : r.asserts)
      std::cout << (a.pass ? "  pass " : "  FAIL ") << "t=" << a.tMs << "ms " << a.check << ": " << a.detail << "\n";
    std::cout << (r.passed() ? "PASS  " : "FAIL  ") << r.name << "  (" << r.asserts.size() << " checks, " << r.endMs
              << " ms simulated)  screen=" << r.finalScreen << "  menu=" << r.finalMenuPath
              << "  fb_hash=" << r.finalHash << "\n";
    if (!r.passed()) failed++;
  }
  std::cout << (scripts.size() - failed) << "/" << scripts.size() << " scripts passed\n";
  return failed == 0 ? 0 : 1;
}

}  // namespace simcli

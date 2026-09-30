// sim_headless - runs mock-scripts against the firmware with no window at all. Prints one line per
// assertion and a summary; exit code 0 only if every script loaded and every assertion passed.
//
//   sim_headless [--project DIR] [--dump-final FILE.pbm] [--keep-storage] script.json|scripts-dir ...
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "core/script.h"

namespace fs = std::filesystem;

static int usage() {
  std::cerr << "usage: sim_headless [--project DIR] [--dump-final FILE.pbm] [--keep-storage] "
               "script.json|scripts-dir ...\n"
               "  --project DIR       folder with storage/ and seed folders (default: parent of the "
               "script's folder)\n"
               "  --dump-final FILE   write the last frame of the (single) script as a PBM image\n"
               "  --keep-storage      keep each run's storage copy and print where it is\n";
  return 2;
}

int main(int argc, char** argv) {
  sim::RunOptions opt;
  bool projectGiven = false;
  std::vector<fs::path> scripts;

  for (int i = 1; i < argc; i++) {
    const std::string a = argv[i];
    if (a == "--project" && i + 1 < argc) {
      opt.projectDir = argv[++i];
      projectGiven = true;
    } else if (a == "--dump-final" && i + 1 < argc) {
      opt.dumpFinal = argv[++i];
    } else if (a == "--keep-storage") {
      opt.keepStorage = true;
    } else if (a == "-h" || a == "--help") {
      return usage();
    } else if (!a.empty() && a[0] == '-') {
      std::cerr << "unknown option " << a << "\n";
      return usage();
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
  if (scripts.empty()) return usage();
  if (!opt.dumpFinal.empty() && scripts.size() > 1) {
    std::cerr << "--dump-final needs exactly one script\n";
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
      std::cout << (a.pass ? "  pass " : "  FAIL ") << "t=" << a.tMs << "ms " << a.check << ": " << a.detail
                << "\n";
    std::cout << (r.passed() ? "PASS  " : "FAIL  ") << r.name << "  (" << r.asserts.size() << " checks, "
              << r.endMs << " ms simulated)  menu=" << r.finalMenuPath << "  fb_hash=" << r.finalHash << "\n";
    if (!r.passed()) failed++;
  }
  std::cout << (scripts.size() - failed) << "/" << scripts.size() << " scripts passed\n";
  return failed == 0 ? 0 : 1;
}

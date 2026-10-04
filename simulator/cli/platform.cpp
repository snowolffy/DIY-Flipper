// platform.cpp - the few OS-specific bits of `sim`: the session file, starting the background server,
// finding a Chromium-family browser, opening windows and folders.
#include <cstdlib>
#include <fstream>
#include <random>
#include <sstream>

#include "cli/session.h"
#include "nlohmann/json.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#else
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace simcli {

fs::path sessionFile(const fs::path& project) { return project / ".sim-session.json"; }

bool readSession(const fs::path& project, SessionInfo& out) {
  std::ifstream f(sessionFile(project));
  if (!f) return false;
  const json j = json::parse(f, nullptr, false);
  if (j.is_discarded()) return false;
  out.port = j.value("port", 0);
  out.token = j.value("token", "");
  out.pid = j.value("pid", 0L);
  out.project = j.value("project", "");
  out.window = j.value("window", false);
  return out.port > 0 && !out.token.empty();
}

bool writeSession(const fs::path& project, const SessionInfo& s) {
  std::ofstream f(sessionFile(project));
  f << json{{"port", s.port}, {"token", s.token}, {"pid", s.pid}, {"project", s.project}, {"window", s.window}}.dump(1);
  return (bool)f;
}

void removeSession(const fs::path& project) {
  std::error_code ec;
  fs::remove(sessionFile(project), ec);
}

fs::path resolveProject(const std::string& given) {
  if (!given.empty()) return fs::absolute(given).lexically_normal();
  const fs::path sim = fs::current_path() / "sim";
  if (fs::is_directory(sim / "scripts") || fs::is_directory(sim / "storage")) return sim;
  return fs::current_path();
}

std::string randomToken() {
  std::random_device rd;
  std::ostringstream s;
  for (int i = 0; i < 4; i++) s << std::hex << rd();
  return s.str();
}

std::string selfExe() {
#ifdef _WIN32
  char buf[MAX_PATH];
  GetModuleFileNameA(nullptr, buf, MAX_PATH);
  return buf;
#else
  std::error_code ec;
  const fs::path p = fs::read_symlink("/proc/self/exe", ec);
  return ec ? std::string("sim") : p.string();
#endif
}

#ifdef _WIN32
static std::string quote(const std::string& a) {
  if (a.find_first_of(" \t\"") == std::string::npos) return a;
  std::string q = "\"";
  for (char c : a) q += c == '"' ? std::string("\\\"") : std::string(1, c);
  return q + "\"";
}
#endif

bool spawnServer(const std::vector<std::string>& args, std::string& err) {
  const std::string exe = selfExe();
#ifdef _WIN32
  std::string cmd = quote(exe);
  for (const auto& a : args) cmd += " " + quote(a);
  STARTUPINFOA si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  std::vector<char> buf(cmd.begin(), cmd.end());
  buf.push_back(0);
  if (!CreateProcessA(nullptr, buf.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP, nullptr,
                      nullptr, &si, &pi)) {
    err = "couldn't start the session process";
    return false;
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
#else
  const pid_t pid = fork();
  if (pid < 0) {
    err = "fork failed";
    return false;
  }
  if (pid == 0) {
    setsid();
    if (fork() != 0) _exit(0);  // grandchild keeps running, reparented away from the terminal
    const int null = open("/dev/null", O_RDWR);
    if (null >= 0) {
      dup2(null, 0);
      dup2(null, 1);
      dup2(null, 2);
    }
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(exe.c_str()));
    for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    execv(exe.c_str(), argv.data());
    _exit(127);
  }
  return true;
#endif
}

std::string findBrowser() {
  if (const char* e = std::getenv("SIM_BROWSER"))
    if (*e) return e;
  std::vector<std::string> candidates;
#ifdef _WIN32
  for (const char* env : {"ProgramFiles(x86)", "ProgramFiles", "LOCALAPPDATA"}) {
    const char* base = std::getenv(env);
    if (!base) continue;
    candidates.push_back(std::string(base) + "\\Microsoft\\Edge\\Application\\msedge.exe");
    candidates.push_back(std::string(base) + "\\Google\\Chrome\\Application\\chrome.exe");
  }
#else
  if (const char* path = std::getenv("PATH")) {
    std::stringstream ss(path);
    std::string dir;
    while (std::getline(ss, dir, ':'))
      for (const char* n : {"chromium", "chromium-browser", "google-chrome", "google-chrome-stable", "microsoft-edge"})
        candidates.push_back(dir + "/" + n);
  }
  // Playwright's bundled Chromium (cloud containers)
  std::error_code ec;
  const char* pw = std::getenv("PLAYWRIGHT_BROWSERS_PATH");
  const fs::path pwRoot = pw ? pw : "/opt/pw-browsers";
  if (fs::is_directory(pwRoot, ec))
    for (const auto& d : fs::directory_iterator(pwRoot, ec)) {
      candidates.push_back((d.path() / "chrome-linux" / "chrome").string());
      candidates.push_back((d.path() / "chrome-linux64" / "chrome").string());
    }
  candidates.push_back((pwRoot / "chromium").string());
#endif
  for (const auto& c : candidates) {
    std::error_code ec;
    if (fs::is_regular_file(c, ec) || (fs::is_symlink(c, ec) && fs::exists(c, ec))) return c;
  }
  return "";
}

bool openWindow(const std::string& url) {
  const std::string browser = findBrowser();
  if (!browser.empty()) {
    // its own profile, so an app window opens even while the browser is already running
    const std::string profile = (fs::temp_directory_path() / "diyf-sim-window").string();
#ifdef _WIN32
    std::string cmd = quote(browser) + " --app=" + quote(url) + " --user-data-dir=" + quote(profile) + " --window-size=1440,900";
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<char> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    if (CreateProcessA(nullptr, buf.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS, nullptr, nullptr, &si, &pi)) {
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
      return true;
    }
#else
    const pid_t pid = fork();
    if (pid == 0) {
      setsid();
      const int null = open("/dev/null", O_RDWR);
      if (null >= 0) dup2(null, 1), dup2(null, 2);
      const std::string app = "--app=" + url, prof = "--user-data-dir=" + profile;
      execl(browser.c_str(), browser.c_str(), app.c_str(), prof.c_str(), "--window-size=1440,900", (char*)nullptr);
      _exit(127);
    }
    if (pid > 0) return true;
#endif
  }
  // no Chromium-family browser: the default one
#ifdef _WIN32
  return (INT_PTR)ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL) > 32;
#else
  return std::system(("xdg-open '" + url + "' >/dev/null 2>&1 &").c_str()) == 0;
#endif
}

bool openFolder(const fs::path& p) {
#ifdef _WIN32
  return (INT_PTR)ShellExecuteA(nullptr, "open", p.string().c_str(), nullptr, nullptr, SW_SHOWNORMAL) > 32;
#else
  return std::system(("xdg-open '" + p.string() + "' >/dev/null 2>&1 &").c_str()) == 0;
#endif
}

}  // namespace simcli

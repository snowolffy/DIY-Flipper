// client.cpp - the terminal commands of `sim` that talk to a running session (session.h). They send the
// same command objects the window sends, so `sim state` and the window always agree.
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <thread>

#include "cli/cli.h"
#include "cli/session.h"
#include "cli/words.h"
#include "httplib/httplib.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace simcli {

namespace {

struct Conn {
  SessionInfo info;
  std::unique_ptr<httplib::Client> http;
  bool open(const fs::path& project) {
    if (!readSession(project, info)) return false;
    http = std::make_unique<httplib::Client>("127.0.0.1", info.port);
    http->set_default_headers({{"X-Sim-Token", info.token}});
    http->set_read_timeout(120, 0);
    auto r = http->Get("/api/state?full=0");
    return r && r->status == 200;
  }
  std::string url() const { return "http://127.0.0.1:" + std::to_string(info.port) + "/?token=" + info.token; }
};

std::string takeOpt(std::vector<std::string>& args, const char* name) {
  for (size_t i = 0; i + 1 < args.size(); i++)
    if (args[i] == name) {
      const std::string v = args[i + 1];
      args.erase(args.begin() + (long)i, args.begin() + (long)i + 2);
      return v;
    }
  return "";
}

bool takeFlag(std::vector<std::string>& args, const char* name) {
  for (size_t i = 0; i < args.size(); i++)
    if (args[i] == name) {
      args.erase(args.begin() + (long)i);
      return true;
    }
  return false;
}

bool startSession(const fs::path& project, bool window, Conn& c) {
  std::string err;
  std::vector<std::string> a = {"serve", "--project", project.string()};
  if (window) a.push_back("--window");
  removeSession(project);
  if (!spawnServer(a, err)) {
    std::cerr << err << "\n";
    return false;
  }
  for (int i = 0; i < 100; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if (c.open(project)) return true;
  }
  std::cerr << "the session didn't start (see " << sessionFile(project).string() << ")\n";
  return false;
}

void printState(const json& s) {
  auto line = [](const char* k, const std::string& v) { std::printf("%-11s%s\n", k, v.c_str()); };
  line("screen", s.value("screen", "") + "   " + s.value("menu_path", ""));
  line("uptime", std::to_string(s.value("uptime_ms", 0u) / 1000) + " s" + (s.value("realtime", false) ? "   real time (window open)" : "   virtual time"));
  const json& p = s["power"];
  line("power", p.value("state", "") + "   switch " + (p.value("switch_on", true) ? "on" : "off") + "   usb " + p["usb"].dump() +
                    "   firmware " + p.value("firmware", ""));
  const json& b = s["battery"];
  line("battery", std::to_string(b.value("set_percent", 0)) + "% set, firmware reads " + std::to_string(b.value("firmware_percent", 0)) + "%");
  line("rtc", s["rtc"].is_null() ? "missing" : s["rtc"].get<std::string>());
  line("sd", std::string(s["sd"].value("present", false) ? "inserted" : "out") + (s["sd"].value("fail_writes", false) ? "   fails every write" : ""));
  line("backlight", std::to_string(s.value("backlight", 0) * 100 / 255) + "%   buzzer " +
                        (s["buzzer"].value("sounding_hz", 0) ? std::to_string(s["buzzer"].value("sounding_hz", 0)) + " Hz" : "silent"));
  line("ir", std::string(s["ir"].value("listening", false) ? "listening" : "not listening") + "   sent " + std::to_string(s["ir"].value("sent_count", 0)));
  const json& n = s["nfc"];
  line("nfc", std::string(n.value("polling", false) ? "polling" : "not polling") + (n.value("card_present", false) ? "   card " + n.value("card_uid", "") : "") +
                  (n.value("emulating", false) ? "   emulating " + n.value("emulated_uid", "") : ""));
  line("wifi", s["wifi"].value("state", "") + (s["wifi"].value("ssid", "").empty() ? "" : "   " + s["wifi"].value("ssid", "")));
  line("bluetooth", s["ble"].value("state", "") + (s["ble"].value("host", "").empty() ? "" : "   " + s["ble"].value("host", "")) + "   keys sent " +
                        std::to_string(s["ble"].value("keys_sent", 0)));
  const json& d = s["display"];
  line("display", "hash " + d.value("hash", "") + "   pushes " + std::to_string(d.value("pushes", 0)) + "   " + std::to_string(d.value("fps", 0)) +
                      " fps   " + std::to_string(d.value("last_push_ms", 0.0)).substr(0, 5) + " ms/push");
  if (!s.value("theme", "").empty()) line("theme", s.value("theme", ""));
}

}  // namespace

int cmdSession(const std::string& cmd, const std::vector<std::string>& argsIn) {
  std::vector<std::string> args = argsIn;
  const fs::path project = resolveProject(takeOpt(args, "--project"));
  Conn c;

  if (cmd == "open" || cmd == "gui") {
    const bool window = cmd == "gui";
    if (!c.open(project) && !startSession(project, window, c)) return 1;
    std::printf("project %s · board ESP32-S3 N16R8 · session on 127.0.0.1:%d\n", project.string().c_str(), c.info.port);
    if (window) {
      if (!openWindow(c.url())) {
        std::cerr << "couldn't open a window; open " << c.url() << " in a browser\n";
        return 1;
      }
      std::printf("window: %s\n", c.url().c_str());
    }
    return 0;
  }
  if (cmd == "set" && !args.empty() && (args[0] == "--help" || args[0] == "-h")) {
    std::printf("%s", setHelp());
    return 0;
  }
  if (!c.open(project)) {
    std::cerr << "no session for " << project.string() << " - start one with `sim open` (or `sim gui`)\n";
    return 1;
  }
  if (cmd == "close") {
    c.http->Post("/api/close", "{}", "application/json");
    // wait until it is really gone, so a following `sim open` doesn't find the old one
    for (int i = 0; i < 60; i++) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      auto r = c.http->Get("/api/state?full=0");
      if (!r) break;
    }
    removeSession(project);
    std::printf("session closed\n");
    return 0;
  }
  if (cmd == "url") {
    std::printf("%s\n", c.url().c_str());
    return 0;
  }
  if (cmd == "state") {
    const bool asJson = takeFlag(args, "--json");
    auto r = c.http->Get("/api/state?full=0");
    if (!r || r->status != 200) return 1;
    const json s = json::parse(r->body);
    if (asJson) std::printf("%s\n", s.dump(1).c_str());
    else printState(s);
    return 0;
  }
  if (cmd == "shot") {
    const std::string scale = takeOpt(args, "--scale");
    if (args.empty()) {
      std::cerr << "usage: sim shot FILE.png [--scale N]\n";
      return 2;
    }
    auto r = c.http->Get("/api/screen.png?scale=" + (scale.empty() ? std::string("1") : scale));
    if (!r || r->status != 200) return 1;
    std::ofstream f(args[0], std::ios::binary);
    f.write(r->body.data(), (std::streamsize)r->body.size());
    std::printf("wrote %s (%dx%d, color)\n", args[0].c_str(), 128 * std::max(1, std::atoi(scale.c_str())),
                160 * std::max(1, std::atoi(scale.c_str())));
    return f ? 0 : 1;
  }
  if (cmd == "shot-ui") {
    if (args.empty()) {
      std::cerr << "usage: sim shot-ui FILE.png [--size 1440x900]\n";
      return 2;
    }
    std::string size = takeOpt(args, "--size");
    if (size.empty()) size = "1440,900";
    for (char& ch : size)
      if (ch == 'x') ch = ',';
    const std::string browser = findBrowser();
    if (browser.empty()) {
      std::cerr << "shot-ui needs Chrome, Chromium or Edge and none was found (set SIM_BROWSER to its path); every "
                   "other command works without it\n";
      return 3;
    }
    const fs::path out = fs::absolute(args[0]);
    const fs::path profile = fs::temp_directory_path() / "diyf-sim-shot";
    std::string line = "\"" + browser + "\" --headless=new --no-sandbox --disable-gpu --hide-scrollbars --user-data-dir=\"" +
                       profile.string() + "\" --window-size=" + size + " --virtual-time-budget=5000 --screenshot=\"" +
                       out.string() + "\" \"" + c.url() + "&shot=1\"";
#ifdef _WIN32
    line = "\"" + line + "\"";  // cmd.exe strips one pair of quotes
    line += " >NUL 2>&1";
#else
    line += " >/dev/null 2>&1";
#endif
    const int rc = std::system(line.c_str());
    if (rc != 0 || !fs::exists(out)) {
      std::cerr << "the browser couldn't take the screenshot (" << browser << ")\n";
      return 1;
    }
    std::printf("wrote %s (whole app window)\n", out.string().c_str());
    return 0;
  }
  if (cmd == "import" && !args.empty()) args[0] = fs::absolute(args[0]).string();
  if (cmd == "cmd") {
    if (args.empty()) return 2;
    auto r = c.http->Post("/api/cmd", args[0], "application/json");
    if (!r) return 1;
    const json j = json::parse(r->body, nullptr, false);
    std::printf("%s\n", j.value("ok", false) ? "ok" : j.value("error", "failed").c_str());
    return j.value("ok", false) ? 0 : 1;
  }
  if (cmd == "set" && !args.empty() && (args[0] == "--help" || args[0] == "-h")) {
    std::printf("%s", setHelp());
    return 0;
  }
  // press / hold / down / up / wait / set / import / restart: terminal words -> command
  std::vector<std::string> words = {cmd};
  words.insert(words.end(), args.begin(), args.end());
  json body = words;
  auto r = c.http->Post("/api/words", body.dump(), "application/json");
  if (!r) {
    std::cerr << "the session didn't answer\n";
    return 1;
  }
  const json j = json::parse(r->body, nullptr, false);
  if (!j.value("ok", false)) {
    std::cerr << j.value("error", "failed") << "\n";
    return 1;
  }
  const std::string info = j.value("info", "");
  // a press from the terminal lets the screen settle (transitions are up to 400 ms), so the next
  // `sim shot` shows where the press led; it appears in the log as its own wait
  if (cmd == "press" || cmd == "hold") c.http->Post("/api/words", json({"wait", "300"}).dump(), "application/json");
  std::printf("ok%s\n", info.empty() ? "" : ("  (" + info + ")").c_str());
  return 0;
}

}  // namespace simcli

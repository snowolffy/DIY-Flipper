// server.cpp - `sim serve`: the session process. Owns one emulated device on the project's real storage
// folder, serves the window (embedded web files) and the API on 127.0.0.1 only. Every change to the device
// arrives as a command object (core/commands.h) - from the window, the terminal or a script.
//
// Time: while a window is connected the device runs in real time; otherwise virtual time moves only when a
// command moves it (press, hold, wait), so terminal sessions repeat exactly.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>

#include "board/board_profile.h"
#include "cli/cli.h"
#include "cli/session.h"
#include "cli/web_assets.h"
#include "cli/words.h"
#include "core/commands.h"
#include "core/importer.h"
#include "core/script.h"
#include "httplib/httplib.h"

#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;
using Clock = std::chrono::steady_clock;

namespace simcli {

namespace {

constexpr int kWindowTimeoutMs = 4000;  // a window that stops polling for this long is gone

struct LogLine {
  uint32_t t;  // device time (ms)
  json cmd;
  std::string text;
  std::string result;
};

struct PendingUp {
  hal::Button b;
  uint32_t at;
};

struct Session {
  fs::path project, storage;
  std::unique_ptr<sim::Simulator> sim;
  std::mutex m;
  std::deque<LogLine> log;
  std::vector<PendingUp> pending;
  uint32_t downAt[hal::kButtonCount] = {};
  bool paused = false;
  bool ownedByWindow = false;
  bool windowSeen = false;
  Clock::time_point lastWindow{};
  Clock::time_point wallBase{};
  uint32_t simBase = 0;
  bool realtime = false;
  std::atomic<bool> quit{false};
  // scripts
  sim::ScriptPlayer player;
  std::string playing;
  json results = json::object();  // name -> {passed, checks, failed: [...]}
  // record
  bool recording = false;
  size_t recordFrom = 0;
  uint32_t recordT0 = 0;
  std::string token;
  int port = 0;

  bool windowConnected() const {
    return windowSeen && std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - lastWindow).count() < kWindowTimeoutMs;
  }
  sim::CmdContext ctx() { return sim::CmdContext{*sim, project, !realtime}; }
};

json filesTree(const fs::path& dir) {
  json out = json::array();
  std::error_code ec;
  std::vector<fs::directory_entry> entries;
  if (fs::is_directory(dir, ec))
    for (const auto& e : fs::directory_iterator(dir, ec))
      if (e.path().filename() != ".gitkeep") entries.push_back(e);
  std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.path().filename() < b.path().filename(); });
  for (const auto& e : entries) {
    json n = {{"name", e.path().filename().string()}};
    if (e.is_directory(ec)) n["children"] = filesTree(e.path());
    else n["size"] = e.file_size(ec);
    out.push_back(n);
  }
  return out;
}

json scriptsList(Session& s) {
  json out = json::array();
  std::vector<std::string> names;
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(s.project / "scripts", ec))
    if (e.path().extension() == ".json") names.push_back(e.path().stem().string());
  std::sort(names.begin(), names.end());
  for (const auto& n : names) out.push_back({{"name", n}, {"result", s.results.value(n, json(nullptr))}});
  return out;
}

json pinsJson() {
  json p = json::array();
  for (const auto& x : board::kPins) p.push_back({{"signal", x.signal}, {"part", x.part}, {"gpio", x.gpio}});
  return p;
}

constexpr uint32_t kMinPressMs = 50;  // a real press lasts at least a few loop ticks

// One command from anywhere: run it, log it in the terminal's words.
sim::CmdResult runLogged(Session& s, const json& cmd) {
  // a key tapped faster than the firmware's loop would vanish: keep it down kMinPressMs
  if (s.realtime && cmd.value("type", "") == "button") {
    hal::Button b;
    const std::string a = cmd.value("action", "");
    if (sim::parseButton(cmd.value("button", ""), b)) {
      if (a == "down") s.downAt[(int)b] = s.sim->now();
      if (a == "up" && s.sim->now() - s.downAt[(int)b] < kMinPressMs) {
        s.pending.push_back({b, s.downAt[(int)b] + kMinPressMs});
        s.log.push_back({s.sim->now(), cmd, describe(cmd), ""});
        return sim::CmdResult{};
      }
    }
  }
  sim::CmdContext ctx = s.ctx();
  const sim::CmdResult r = sim::runCommand(ctx, cmd);
  if (s.realtime && cmd.value("type", "") == "button") {
    const std::string a = cmd.value("action", "press");
    if (a == "press" || a == "hold") {
      hal::Button b;
      sim::parseButton(cmd.value("button", ""), b);
      s.pending.push_back({b, s.sim->now() + cmd.value("duration_ms", a == "press" ? 80u : 800u)});
    }
  }
  s.log.push_back({s.sim->now(), cmd, describe(cmd), r.ok ? r.info : r.error});
  while (s.log.size() > 500) {
    s.log.pop_front();
    if (s.recordFrom > 0) s.recordFrom--;
  }
  return r;
}

json stateFor(Session& s, bool full) {
  json j = sim::stateJson(*s.sim);
  j["project"] = s.project.string();
  j["storage"] = s.storage.string();
  j["paused"] = s.paused;
  j["realtime"] = s.realtime;
  j["recording"] = s.recording;
  j["playing"] = s.playing;
  j["held"] = json::array();
  for (int b = 0; b < hal::kButtonCount; b++)
    if (s.sim->input().isDown((hal::Button)b)) j["held"].push_back(sim::buttonName((hal::Button)b));
  if (full) {
    j["board"] = {{"name", board::kName}, {"display", board::kDisplayName}, {"spi_hz", board::kDisplaySpiHz},
                  {"max_fps", board::kMaxFps}, {"pins", pinsJson()}};
    json lines = json::array();
    const size_t from = s.log.size() > 200 ? s.log.size() - 200 : 0;
    for (size_t i = from; i < s.log.size(); i++)
      lines.push_back({{"t", s.log[i].t}, {"text", s.log[i].text}, {"result", s.log[i].result}});
    j["log"] = lines;
    j["scripts"] = scriptsList(s);
    json seeds = json::array();
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(s.project / "seeds", ec))
      if (e.is_directory(ec)) seeds.push_back(e.path().filename().string());
    j["seeds"] = seeds;
  }
  return j;
}

json scriptFromLog(Session& s, size_t from, uint32_t t0) {
  json events = json::array();
  for (size_t i = from; i < s.log.size(); i++) {
    json e = s.log[i].cmd;
    if (e.value("type", "") == "wait") continue;  // time is in t_ms
    e["t_ms"] = s.log[i].t >= t0 ? s.log[i].t - t0 : 0;
    events.push_back(e);
  }
  return {{"name", "recorded"}, {"initial_state", json::object()}, {"events", events}};
}

void tickLoop(Session& s) {
  while (!s.quit) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::lock_guard<std::mutex> lock(s.m);
    const bool rt = s.windowConnected() && !s.paused;
    if (rt && !s.realtime) {
      s.wallBase = Clock::now();
      s.simBase = s.sim->now();
    }
    s.realtime = rt;
    if (s.ownedByWindow && s.windowSeen && !s.windowConnected()) {
      s.quit = true;  // the window was closed: a `sim gui` session ends with it
      break;
    }
    if (!rt) continue;
    const uint32_t target =
        s.simBase + (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - s.wallBase).count();
    // button releases scheduled by press/hold commands, in time order
    std::sort(s.pending.begin(), s.pending.end(), [](const PendingUp& a, const PendingUp& b) { return a.at < b.at; });
    while (!s.pending.empty() && s.pending.front().at <= target) {
      s.sim->advanceTo(s.pending.front().at);
      s.sim->setButton(s.pending.front().b, false);
      s.pending.erase(s.pending.begin());
    }
    if (s.player.active()) {
      sim::CmdContext ctx = s.ctx();
      s.player.runUntil(ctx, target);
      if (s.player.finished()) {
        int failed = 0;
        json fails = json::array();
        for (const auto& a : s.player.results())
          if (!a.pass) failed++, fails.push_back(a.check + ": " + a.detail);
        s.results[s.playing] = {{"passed", failed == 0}, {"checks", s.player.results().size()}, {"failed", fails}, {"where", "here"}};
        s.player.stop();
        s.playing.clear();
      }
    }
    s.sim->advanceTo(target);
  }
}

std::string arg(const std::vector<std::string>& args, const char* name, const std::string& def = "") {
  for (size_t i = 0; i + 1 < args.size(); i++)
    if (args[i] == name) return args[i + 1];
  return def;
}

bool hasFlag(const std::vector<std::string>& args, const char* name) {
  return std::find(args.begin(), args.end(), name) != args.end();
}

}  // namespace

// sim serve --project DIR [--window] [--port N]
int cmdServe(const std::vector<std::string>& args) {
  Session s;
  s.project = resolveProject(arg(args, "--project"));
  s.storage = s.project / "storage";
  s.ownedByWindow = hasFlag(args, "--window");
  std::error_code ec;
  fs::create_directories(s.storage / "flash", ec);
  fs::create_directories(s.storage / "sd", ec);
  s.sim = std::make_unique<sim::Simulator>(s.storage);
  s.sim->rtc().set([] {
    hal::DateTime t;
    sim::MockRtc::parse("2026-10-03T12:34:00", t);
    return t;
  }());
  s.sim->boot();
  s.token = randomToken();

  httplib::Server srv;
  auto allowed = [&](const httplib::Request& req) {
    // only this machine, only this page: Host must be the loopback address (no DNS rebinding), an Origin
    // when present must be ours, and every API call carries the session token
    const std::string host = req.get_header_value("Host");
    const std::string p = std::to_string(s.port);
    if (host != "127.0.0.1:" + p && host != "localhost:" + p) return false;
    const std::string origin = req.get_header_value("Origin");
    if (!origin.empty() && origin != "http://127.0.0.1:" + p && origin != "http://localhost:" + p) return false;
    return true;
  };
  auto authed = [&](const httplib::Request& req) {
    if (!allowed(req)) return false;
    const std::string t = req.has_header("X-Sim-Token") ? req.get_header_value("X-Sim-Token") : req.get_param_value("token");
    return t == s.token;
  };
  srv.set_pre_routing_handler([&](const httplib::Request& req, httplib::Response& res) {
    const bool api = req.path.rfind("/api/", 0) == 0 || req.path == "/";
    if ((api && !authed(req)) || (!api && !allowed(req))) {
      res.status = 403;
      res.set_content("forbidden", "text/plain");
      return httplib::Server::HandlerResponse::Handled;
    }
    if (req.get_header_value("X-Sim-Window") == "1") {
      std::lock_guard<std::mutex> lock(s.m);
      s.windowSeen = true;
      s.lastWindow = Clock::now();
    }
    return httplib::Server::HandlerResponse::Unhandled;
  });

  // ---- the window ----
  auto serveFile = [](const std::string& path, httplib::Response& res) {
    for (size_t i = 0; i < kWebFileCount; i++)
      if (path == kWebFiles[i].path) {
        res.set_content(std::string((const char*)kWebFiles[i].data, kWebFiles[i].size), kWebFiles[i].type);
        res.set_header("Cache-Control", "no-store");
        return true;
      }
    return false;
  };
  srv.Get("/", [&](const httplib::Request&, httplib::Response& res) { serveFile("index.html", res); });
  srv.Get(R"(/assets/(.+))", [&](const httplib::Request& req, httplib::Response& res) {
    if (!serveFile(req.matches[1], res)) res.status = 404;
  });

  // ---- API ----
  auto reply = [](httplib::Response& res, const json& j, int status = 200) {
    res.status = status;
    res.set_content(j.dump(), "application/json");
  };
  srv.Get("/api/state", [&](const httplib::Request& req, httplib::Response& res) {
    std::lock_guard<std::mutex> lock(s.m);
    reply(res, stateFor(s, req.get_param_value("full") != "0"));
  });
  srv.Get("/api/frame", [&](const httplib::Request&, httplib::Response& res) {
    std::lock_guard<std::mutex> lock(s.m);
    // 128x160 RGB565 little-endian + backlight level as the last byte
    std::string body((const char*)s.sim->display().frame(), ui::kFrameBytes);
    body += (char)s.sim->backlight().level();
    res.set_content(body, "application/octet-stream");
  });
  srv.Get("/api/screen.png", [&](const httplib::Request& req, httplib::Response& res) {
    std::lock_guard<std::mutex> lock(s.m);
    const int scale = std::max(1, std::min(8, std::atoi(req.get_param_value("scale").c_str())));
    const std::vector<uint8_t> png = s.sim->display().png(scale);
    res.set_content(std::string(png.begin(), png.end()), "image/png");
  });
  srv.Post("/api/cmd", [&](const httplib::Request& req, httplib::Response& res) {
    const json cmd = json::parse(req.body, nullptr, false);
    if (cmd.is_discarded()) return reply(res, {{"ok", false}, {"error", "not JSON"}}, 400);
    // wait in real time: let the clock run (outside the lock)
    if (cmd.value("type", "") == "wait") {
      bool rt;
      {
        std::lock_guard<std::mutex> lock(s.m);
        rt = s.realtime;
        if (!rt) {
          runLogged(s, cmd);
          return reply(res, {{"ok", true}, {"state", stateFor(s, false)}});
        }
        runLogged(s, cmd);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(cmd.value("ms", 0)));
      std::lock_guard<std::mutex> lock(s.m);
      return reply(res, {{"ok", true}, {"state", stateFor(s, false)}});
    }
    std::lock_guard<std::mutex> lock(s.m);
    const sim::CmdResult r = runLogged(s, cmd);
    reply(res, {{"ok", r.ok}, {"error", r.error}, {"info", r.info}, {"state", stateFor(s, false)}}, r.ok ? 200 : 400);
  });
  srv.Post("/api/words", [&](const httplib::Request& req, httplib::Response& res) {
    // terminal words, e.g. ["set","battery","40"]
    const json w = json::parse(req.body, nullptr, false);
    json cmd;
    std::string err;
    std::vector<std::string> words;
    if (w.is_array())
      for (const auto& x : w) words.push_back(x.get<std::string>());
    if (!parseWords(words, cmd, err)) return reply(res, {{"ok", false}, {"error", err}}, 400);
    std::lock_guard<std::mutex> lock(s.m);
    const sim::CmdResult r = runLogged(s, cmd);
    reply(res, {{"ok", r.ok}, {"error", r.error}, {"info", r.info}}, r.ok ? 200 : 400);
  });
  srv.Post("/api/pause", [&](const httplib::Request& req, httplib::Response& res) {
    std::lock_guard<std::mutex> lock(s.m);
    s.paused = json::parse(req.body, nullptr, false).value("paused", !s.paused);
    reply(res, {{"paused", s.paused}});
  });
  srv.Get("/api/files", [&](const httplib::Request&, httplib::Response& res) {
    std::lock_guard<std::mutex> lock(s.m);
    reply(res, {{"root", s.storage.string()}, {"tree", filesTree(s.storage)}});
  });
  srv.Post("/api/open-folder", [&](const httplib::Request&, httplib::Response& res) {
    reply(res, {{"ok", openFolder(s.storage)}});
  });
  srv.Post("/api/reset-storage", [&](const httplib::Request& req, httplib::Response& res) {
    const std::string seed = json::parse(req.body, nullptr, false).value("seed", "");
    std::lock_guard<std::mutex> lock(s.m);
    std::error_code e2;
    for (const char* v : {"flash", "sd"}) {
      fs::remove_all(s.storage / v, e2);
      fs::create_directories(s.storage / v, e2);
    }
    std::string err;
    if (!seed.empty() && !sim::copySeed(s.project, "seeds/" + seed, s.storage, err))
      return reply(res, {{"ok", false}, {"error", err}}, 400);
    runLogged(s, {{"type", "restart"}, {"cold_boot", true}});
    reply(res, {{"ok", true}});
  });
  srv.Post("/api/import", [&](const httplib::Request& req, httplib::Response& res) {
    std::string name = fs::path(req.get_param_value("name")).filename().string();
    if (name.empty() || req.body.empty()) return reply(res, {{"ok", false}, {"error", "no file"}}, 400);
    std::error_code e2;
    fs::create_directories(s.project / "imports", e2);
    {
      std::ofstream f(s.project / "imports" / name, std::ios::binary);
      f.write(req.body.data(), (std::streamsize)req.body.size());
    }
    std::lock_guard<std::mutex> lock(s.m);
    const sim::CmdResult r = runLogged(s, {{"type", "import"}, {"file", "imports/" + name}});
    reply(res, {{"ok", r.ok}, {"error", r.error}, {"info", r.info}}, r.ok ? 200 : 400);
  });
  srv.Get("/api/keys", [&](const httplib::Request&, httplib::Response& res) {
    std::ifstream f(s.project / ".sim-keys.json");
    const json k = f ? json::parse(f, nullptr, false) : json();
    reply(res, k.is_object() ? k : json::object());
  });
  srv.Post("/api/keys", [&](const httplib::Request& req, httplib::Response& res) {
    const json k = json::parse(req.body, nullptr, false);
    if (!k.is_object()) return reply(res, {{"ok", false}}, 400);
    std::ofstream f(s.project / ".sim-keys.json");
    f << k.dump(1);
    reply(res, {{"ok", true}});
  });
  srv.Post("/api/script/run", [&](const httplib::Request& req, httplib::Response& res) {
    const json b = json::parse(req.body, nullptr, false);
    const std::string name = b.value("name", "");
    sim::Script sc;
    std::string err;
    if (!sim::loadScript(s.project / "scripts" / (name + ".json"), sc, err)) return reply(res, {{"ok", false}, {"error", err}}, 400);
    std::lock_guard<std::mutex> lock(s.m);
    if (b.value("restart", true)) {
      sim::applyInitialState(sc.init, *s.sim);
      s.sim->restart(sc.init.coldBoot);
    }
    s.results.erase(name);
    s.playing = name;
    s.player.start(std::move(sc), s.sim->now());
    if (!s.realtime) {  // no window: play it through at once in virtual time
      sim::CmdContext ctx = s.ctx();
      s.player.runUntil(ctx, s.sim->now() + s.player.script().lastT());
      int failed = 0;
      json fails = json::array();
      for (const auto& a : s.player.results())
        if (!a.pass) failed++, fails.push_back(a.check + ": " + a.detail);
      s.results[name] = {{"passed", failed == 0}, {"checks", s.player.results().size()}, {"failed", fails}, {"where", "here"}};
      s.player.stop();
      s.playing.clear();
    }
    reply(res, {{"ok", true}});
  });
  srv.Post("/api/script/stop", [&](const httplib::Request&, httplib::Response& res) {
    std::lock_guard<std::mutex> lock(s.m);
    s.player.stop();
    s.playing.clear();
    reply(res, {{"ok", true}});
  });
  srv.Post("/api/script/run-all", [&](const httplib::Request&, httplib::Response& res) {
    // every script headless on its own storage copy: the live device is not touched
    std::vector<std::string> names;
    {
      std::lock_guard<std::mutex> lock(s.m);
      for (const auto& x : scriptsList(s)) names.push_back(x["name"]);
    }
    json out = json::object();
    for (const auto& n : names) {
      sim::RunOptions o;
      o.projectDir = s.project;
      const sim::ScriptResult r = sim::runScript(s.project / "scripts" / (n + ".json"), o);
      json fails = json::array();
      for (const auto& a : r.asserts)
        if (!a.pass) fails.push_back(a.check + ": " + a.detail);
      if (!r.loaded) fails.push_back(r.error);
      out[n] = {{"passed", r.passed()}, {"checks", r.asserts.size()}, {"failed", fails}, {"where", "headless"}};
    }
    std::lock_guard<std::mutex> lock(s.m);
    for (auto it = out.begin(); it != out.end(); ++it) s.results[it.key()] = it.value();
    reply(res, out);
  });
  srv.Post("/api/record", [&](const httplib::Request& req, httplib::Response& res) {
    const json b = json::parse(req.body, nullptr, false);
    std::lock_guard<std::mutex> lock(s.m);
    if (b.value("start", false)) {
      s.recording = true;
      s.recordFrom = s.log.size();
      s.recordT0 = s.sim->now();
      return reply(res, {{"ok", true}});
    }
    if (!s.recording) return reply(res, {{"ok", false}, {"error", "not recording"}}, 400);
    s.recording = false;
    const std::string name = b.value("name", "recorded");
    json doc = scriptFromLog(s, s.recordFrom, s.recordT0);
    doc["name"] = name;
    std::error_code e2;
    fs::create_directories(s.project / "scripts", e2);
    std::ofstream f(s.project / "scripts" / (name + ".json"));
    f << doc.dump(1) << "\n";
    reply(res, {{"ok", true}, {"path", "scripts/" + name + ".json"}});
  });
  srv.Post("/api/save-log", [&](const httplib::Request& req, httplib::Response& res) {
    const std::string name = json::parse(req.body, nullptr, false).value("name", "session");
    std::lock_guard<std::mutex> lock(s.m);
    json doc = scriptFromLog(s, 0, s.log.empty() ? 0 : s.log.front().t);
    doc["name"] = name;
    std::error_code e2;
    fs::create_directories(s.project / "scripts", e2);
    std::ofstream f(s.project / "scripts" / (name + ".json"));
    f << doc.dump(1) << "\n";
    reply(res, {{"ok", true}, {"path", "scripts/" + name + ".json"}});
  });
  srv.Post("/api/close", [&](const httplib::Request&, httplib::Response& res) {
    reply(res, {{"ok", true}});
    s.quit = true;
  });

  // ---- start ----
  s.port = srv.bind_to_any_port("127.0.0.1");
  if (s.port <= 0) {
    std::cerr << "couldn't open a port on 127.0.0.1\n";
    return 1;
  }
  SessionInfo info;
  info.port = s.port;
  info.token = s.token;
  info.pid = (long)getpid();
  info.project = s.project.string();
  info.window = s.ownedByWindow;
  writeSession(s.project, info);

  std::thread ticker([&] { tickLoop(s); });
  std::thread watcher([&] {
    while (!s.quit) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    srv.stop();
  });
  srv.listen_after_bind();
  s.quit = true;
  ticker.join();
  watcher.join();
  SessionInfo still;
  if (readSession(s.project, still) && still.port == s.port) removeSession(s.project);
  return 0;
}

}  // namespace simcli

#include "core/commands.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <fstream>
#include <sstream>

#include "app/theme.h"
#include "board/board_profile.h"
#include "core/importer.h"

namespace fs = std::filesystem;

namespace sim {

namespace {

std::string str(const json& j, const char* key) {
  auto it = j.find(key);
  return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

// 4, "4", "0x04" all read as 4.
uint32_t num(const json& j, const char* key) {
  auto it = j.find(key);
  if (it == j.end()) return 0;
  if (it->is_number_unsigned() || it->is_number_integer()) return it->get<uint32_t>();
  if (it->is_string()) return (uint32_t)std::strtoul(it->get<std::string>().c_str(), nullptr, 0);
  return 0;
}

fs::path resolve(const CmdContext& ctx, const std::string& p) {
  fs::path f(p);
  return f.is_absolute() ? f : ctx.projectDir / f;
}

const char* kTypes[] = {"button", "wait",     "battery",    "usb",        "rtc",       "sd",
                        "storage", "ir_signal", "nfc_card",  "nfc_reader", "nfc_module", "wifi",
                        "ble_host", "ble_bonds", "power_switch", "restart", "import",    "dump"};

}  // namespace

bool parseButton(const std::string& name, hal::Button& out) {
  std::string n = name;
  std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return (char)std::toupper(c); });
  if (n == "OK") out = hal::Button::Ok;
  else if (n == "CANCEL" || n == "BACK") out = hal::Button::Cancel;
  else if (n == "LEFT" || n == "<") out = hal::Button::Left;
  else if (n == "RIGHT" || n == ">") out = hal::Button::Right;
  else if (n == "POWER") out = hal::Button::Power;
  else return false;
  return true;
}

const char* buttonName(hal::Button b) {
  switch (b) {
    case hal::Button::Ok: return "OK";
    case hal::Button::Cancel: return "CANCEL";
    case hal::Button::Left: return "LEFT";
    case hal::Button::Right: return "RIGHT";
    case hal::Button::Power: return "POWER";
  }
  return "?";
}

bool validateCommand(const json& e, std::string& err) {
  if (!e.is_object()) {
    err = "a command is a JSON object";
    return false;
  }
  const std::string type = str(e, "type");
  if (std::none_of(std::begin(kTypes), std::end(kTypes), [&](const char* t) { return type == t; })) {
    err = "unknown command type '" + type + "'";
    return false;
  }
  if (type == "button") {
    hal::Button b;
    if (!parseButton(str(e, "button"), b)) {
      err = "unknown button '" + str(e, "button") + "'";
      return false;
    }
    const std::string a = e.value("action", std::string("press"));
    if (a != "press" && a != "hold" && a != "down" && a != "up") {
      err = "button action must be press, hold, down or up";
      return false;
    }
  } else if (type == "rtc") {
    hal::DateTime dt;
    const std::string t = str(e, "time");
    if (!e.value("missing", false) && t != "pc" && !MockRtc::parse(t, dt)) {
      err = "rtc needs time (ISO date-time or \"pc\") or missing: true";
      return false;
    }
  } else if (type == "ir_signal") {
    if (str(e, "protocol").empty()) {
      err = "ir_signal needs a protocol";
      return false;
    }
  } else if (type == "nfc_card") {
    const std::string a = e.value("action", std::string("present"));
    if (a != "present" && a != "remove") {
      err = "nfc_card action must be present or remove";
      return false;
    }
    if (a == "present" && str(e, "uid").empty() && str(e, "dump").empty()) {
      err = "nfc_card present needs a uid";
      return false;
    }
  } else if (type == "ble_host") {
    const std::string a = e.value("action", std::string("connect"));
    if (a != "connect" && a != "disconnect") {
      err = "ble_host action must be connect or disconnect";
      return false;
    }
  }
  return true;
}

CmdResult runCommand(CmdContext& ctx, const json& e) {
  CmdResult r;
  if (!validateCommand(e, r.error)) {
    r.ok = false;
    return r;
  }
  Simulator& s = ctx.sim;
  const std::string type = str(e, "type");

  if (type == "button") {
    hal::Button b;
    parseButton(str(e, "button"), b);
    const std::string a = e.value("action", std::string("press"));
    if (a == "down" || a == "up") {
      s.setButton(b, a == "down");
    } else {
      const uint32_t dur = e.value("duration_ms", a == "press" ? 80u : 800u);
      s.setButton(b, true);
      if (ctx.virtualTime) {
        s.advanceBy(dur);
        s.setButton(b, false);
        s.advanceBy(Simulator::kTickMs);
      } else {
        r.info = "release with a button up command";
      }
    }
  } else if (type == "wait") {
    if (ctx.virtualTime) s.advanceBy(e.value("ms", 0u));
  } else if (type == "battery") {
    s.battery().setPercent(e.contains("percent") && e["percent"].is_number() ? e["percent"].get<int>() : -1);
  } else if (type == "usb") {
    const json& p = e.contains("plugged") ? e["plugged"] : json("unknown");
    s.power().setUsb(p.is_boolean() ? (p.get<bool>() ? hal::Usb::Present : hal::Usb::Absent) : hal::Usb::Unknown);
  } else if (type == "rtc") {
    if (e.value("missing", false)) {
      s.rtc().setMissing(true);
    } else {
      hal::DateTime dt;
      const std::string t = str(e, "time");
      if (t == "pc") {
        const std::time_t now = std::time(nullptr);
        std::tm lt{};
#ifdef _WIN32
        localtime_s(&lt, &now);
#else
        localtime_r(&now, &lt);
#endif
        dt.year = (uint16_t)(lt.tm_year + 1900);
        dt.month = (uint8_t)(lt.tm_mon + 1);
        dt.day = (uint8_t)lt.tm_mday;
        dt.hour = (uint8_t)lt.tm_hour;
        dt.minute = (uint8_t)lt.tm_min;
        dt.second = (uint8_t)lt.tm_sec;
      } else {
        MockRtc::parse(t, dt);
      }
      s.rtc().set(dt);
    }
  } else if (type == "sd") {
    s.storage().setSdPresent(e.value("present", true));
  } else if (type == "storage") {
    s.storage().setFailWrites(e.value("fail_writes", false));
  } else if (type == "ir_signal") {
    hal::IrSignal sig;
    sig.protocol = str(e, "protocol");
    sig.address = num(e, "address");
    sig.command = num(e, "command");
    if (e.contains("raw") && e["raw"].is_array())
      for (const json& v : e["raw"]) sig.raw.push_back(v.get<uint16_t>());
    if (e.contains("raw_file")) {
      std::ifstream f(resolve(ctx, str(e, "raw_file")));
      std::stringstream ss;
      ss << f.rdbuf();
      if (!f || !MockIr::parseRaw(ss.str(), sig.raw)) {
        r.ok = false;
        r.error = "can't read timings from " + str(e, "raw_file");
        return r;
      }
    }
    s.ir().inject(sig);
    if (!s.ir().listening()) r.info = "the receiver is off: signal lost (as on the device)";
  } else if (type == "nfc_card") {
    if (e.value("action", std::string("present")) == "remove") {
      s.nfc().remove();
    } else {
      hal::NfcCard c;
      c.uid = str(e, "uid");
      c.type = e.value("card_type", std::string("NTAG215"));
      c.magic = e.value("magic", false);
      if (e.contains("blocks") && e["blocks"].is_array())
        for (const json& v : e["blocks"]) c.blocks.push_back(v.get<std::string>());
      // or a saved dump on the device's storage: the card it was read from
      if (!str(e, "dump").empty()) {
        hal::Volume v;
        std::string path, text;
        if (!MockStorage::parse(str(e, "dump"), v, path) || !s.storage().read(v, path, text)) {
          r.ok = false;
          r.error = "can't read dump " + str(e, "dump");
          return r;
        }
        c.blocks.clear();
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
          if (!line.empty() && line.back() == '\r') line.pop_back();
          if (line.rfind("uid=", 0) == 0) c.uid = line.substr(4);
          else if (line.rfind("type=", 0) == 0) c.type = line.substr(5);
          else if (line.rfind("block=", 0) == 0) c.blocks.push_back(line.substr(6));
        }
      }
      s.nfc().place(c);
    }
  } else if (type == "nfc_reader") {
    s.nfc().readerTap();
    if (!s.nfc().emulating()) r.info = "the device isn't emulating a card";
  } else if (type == "nfc_module") {
    s.nfc().setModulePresent(e.value("present", true));
  } else if (type == "wifi") {
    if (e.contains("networks")) {
      std::vector<hal::WifiNetwork> nets;
      for (const json& n : e["networks"])
        nets.push_back({n.value("ssid", std::string()), n.value("rssi", -60), n.value("secured", true)});
      s.wifi().setNetworks(nets);
    }
    if (e.contains("next_connect_succeeds")) s.wifi().setNextConnectSucceeds(e["next_connect_succeeds"].get<bool>());
    if (e.contains("latency_ms")) s.wifi().setLatencyMs(e["latency_ms"].get<uint32_t>());
    if (e.contains("ntp_responds")) s.wifi().setNtpResponds(e["ntp_responds"].get<bool>());
    if (e.value("drop", false)) s.wifi().drop();
  } else if (type == "ble_host") {
    if (e.value("action", std::string("connect")) == "connect") s.ble().hostConnect(e.value("name", std::string("Phone")));
    else s.ble().hostDisconnect();
  } else if (type == "ble_bonds") {
    s.ble().setBondListFull(e.value("full", true));
  } else if (type == "power_switch") {
    s.setPowerSwitch(e.value("on", true));
  } else if (type == "restart") {
    s.restart(e.value("cold_boot", true));
  } else if (type == "import") {
    const ImportResult res = importAsset(resolve(ctx, str(e, "file")), s.storage());
    r.ok = res.ok;
    r.error = res.error;
    r.info = std::to_string(res.written.size()) + " files";
    for (const auto& w : res.warnings) r.info += "; " + w;
  } else if (type == "dump") {
    const fs::path p = resolve(ctx, str(e, "path"));
    if (!s.display().writePng(p, e.value("scale", 1))) {
      r.ok = false;
      r.error = "couldn't write " + p.string();
    }
  }
  return r;
}

json stateField(Simulator& s, const std::string& f) {
  const json st = stateJson(s);
  // dotted path: "wifi.state"
  const json* cur = &st;
  size_t p = 0;
  while (p <= f.size()) {
    size_t d = f.find('.', p);
    if (d == std::string::npos) d = f.size();
    const std::string k = f.substr(p, d - p);
    if (!cur->is_object() || !cur->contains(k)) return nullptr;
    cur = &(*cur)[k];
    p = d + 1;
  }
  return *cur;
}

json stateJson(Simulator& s) {
  json j;
  const bool on = s.power().state() != PowerState::Off;
  app::App& a = s.app();
  j["uptime_ms"] = s.now();
  j["menu_path"] = on ? a.menuPath() : "";
  j["screen"] = on ? a.screenCode() : "";
  j["events"] = a.lastEvents();
  j["power"] = {{"state", powerStateName(s.power().state())},
                {"switch_on", s.power().switchOn()},
                {"usb", s.power().usb() == hal::Usb::Unknown ? json("unknown") : json(s.power().usb() == hal::Usb::Present)},
                {"firmware", a.powerState()}};
  j["display"] = {{"hash", s.display().hash()},
                  {"pushes", s.display().pushes()},
                  {"fps", s.display().fps(s.now())},
                  {"last_push_ms", s.display().lastPushMs()},
                  {"spi_hz", board::kDisplaySpiHz}};
  j["backlight"] = s.backlight().level();
  j["buzzer"] = {{"sounding_hz", s.buzzer().sounding()}, {"tones", s.buzzer().count()}};
  j["battery"] = {{"set_percent", s.battery().percentSet()}, {"firmware_percent", a.battery().percent()}};
  hal::DateTime t;
  j["rtc"] = s.rtc().missing() || !s.rtc().now(t) ? json(nullptr) : json(MockRtc::format(t));
  j["sd"] = {{"present", s.storage().sdPresent()}, {"fail_writes", s.storage().failWrites()}};
  json sent = json::array();
  for (const auto& x : s.ir().sent())
    sent.push_back({{"protocol", x.protocol}, {"address", x.address}, {"command", x.command}, {"raw", x.raw.size()}});
  j["ir"] = {{"listening", s.ir().listening()}, {"sent_count", s.ir().sentCount()}, {"sent", sent}};
  j["nfc"] = {{"module", s.nfc().modulePresent()},
              {"polling", s.nfc().polling()},
              {"card_present", s.nfc().present()},
              {"card_uid", s.nfc().present() ? s.nfc().cardInField().uid : ""},
              {"emulating", s.nfc().emulating()},
              {"emulated_uid", s.nfc().emulating() ? s.nfc().emulated().uid : ""},
              {"reader_taps", s.nfc().readerTaps()},
              {"blocks_written", s.nfc().blocksWritten()}};
  json nets = json::array();
  for (const auto& n : s.wifi().networks()) nets.push_back({{"ssid", n.ssid}, {"rssi", n.rssi}, {"secured", n.secured}});
  j["wifi"] = {{"state", wifiStateName(s.wifi().state())},
               {"ssid", s.wifi().connectedSsid()},
               {"password", s.wifi().lastPassword()},
               {"networks", nets},
               {"next_connect_succeeds", s.wifi().nextConnectSucceeds()},
               {"ntp_responds", s.wifi().ntpResponds()},
               {"latency_ms", s.wifi().latencyMs()}};
  json keys = json::array();
  for (const auto& k : s.ble().keys())
    keys.push_back({{"page", k.page == hal::HidKey::Consumer ? "consumer" : "keyboard"}, {"usage", k.usage}});
  j["ble"] = {{"state", bleStateName(s.ble().state())},
              {"host", s.ble().hostName()},
              {"passkey", s.ble().state() == hal::BleState::PairingRequest ? json(s.ble().passkey()) : json(nullptr)},
              {"bonds", [&] {
                 json b = json::array();
                 for (const auto& x : s.ble().bonds()) b.push_back(x.name);
                 return b;
               }()},
              {"keys_sent", s.ble().keysSent()},
              {"keys", keys}};
  j["theme"] = theme::activeName();
  return j;
}

}  // namespace sim

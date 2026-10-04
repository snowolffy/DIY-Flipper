#include "cli/words.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace simcli {

namespace {

std::string up(std::string s) {
  for (char& c : s) c = (char)std::toupper((unsigned char)c);
  return s;
}

bool isNum(const std::string& s) {
  if (s.empty()) return false;
  char* end = nullptr;
  std::strtol(s.c_str(), &end, 0);
  return *end == 0;
}

std::string onOff(bool b) { return b ? "on" : "off"; }

}  // namespace

const char* setHelp() {
  return "sim set battery 0-100|unknown\n"
         "sim set usb on|off|unknown\n"
         "sim set sd in|out           sim set sd fail on|off\n"
         "sim set rtc 2026-10-03T12:34:00|pc|missing\n"
         "sim set ir NEC|Samsung|Sony|RC5 ADDRESS COMMAND      sim set ir raw FILE\n"
         "sim set nfc place UID [classic|ntag] [magic]   sim set nfc remove   sim set nfc reader\n"
         "sim set nfc module on|off\n"
         "sim set wifi next ok|fail   sim set wifi ntp on|off   sim set wifi delay MS   sim set wifi drop\n"
         "sim set wifi networks '[{\"ssid\":\"HomeNet\",\"rssi\":-48,\"secured\":true}]'\n"
         "sim set ble connect NAME    sim set ble disconnect    sim set ble full on|off\n"
         "sim set switch on|off       (the power switch)\n";
}

std::string describe(const json& c) {
  const std::string t = c.value("type", "");
  auto num = [&](const char* k) { return c.contains(k) ? c[k].dump() : std::string(); };
  if (t == "button") {
    const std::string a = c.value("action", "press"), b = c.value("button", "");
    if (a == "hold") return "hold " + b + " " + std::to_string(c.value("duration_ms", 800));
    if (a == "press" && c.contains("duration_ms")) return "hold " + b + " " + std::to_string(c.value("duration_ms", 80));
    return a + " " + b;
  }
  if (t == "wait") return "wait " + num("ms");
  if (t == "battery") return "set battery " + (c.contains("percent") && c["percent"].is_number() ? c["percent"].dump() : "unknown");
  if (t == "usb") return "set usb " + (c.contains("plugged") && c["plugged"].is_boolean() ? onOff(c["plugged"]) : "unknown");
  if (t == "sd") return std::string("set sd ") + (c.value("present", true) ? "in" : "out");
  if (t == "storage") return "set sd fail " + onOff(c.value("fail_writes", false));
  if (t == "rtc") return c.value("missing", false) ? "set rtc missing" : "set rtc " + c.value("time", "");
  if (t == "ir_signal") {
    if (c.contains("raw_file")) return "set ir raw " + c.value("raw_file", "");
    return "set ir " + c.value("protocol", "") + " " + num("address") + " " + num("command");
  }
  if (t == "nfc_card") {
    if (c.value("action", "present") == "remove") return "set nfc remove";
    std::string s = "set nfc place " + c.value("uid", "");
    const std::string ty = c.value("card_type", "NTAG215");
    s += ty.find("Classic") != std::string::npos ? " classic" : ty == "NTAG215" ? " ntag" : " \"" + ty + "\"";
    if (c.value("magic", false)) s += " magic";
    return s;
  }
  if (t == "nfc_reader") return "set nfc reader";
  if (t == "nfc_module") return "set nfc module " + onOff(c.value("present", true));
  if (t == "wifi") {
    if (c.value("drop", false)) return "set wifi drop";
    if (c.contains("next_connect_succeeds")) return std::string("set wifi next ") + (c["next_connect_succeeds"].get<bool>() ? "ok" : "fail");
    if (c.contains("ntp_responds")) return "set wifi ntp " + onOff(c["ntp_responds"]);
    if (c.contains("latency_ms")) return "set wifi delay " + num("latency_ms");
    if (c.contains("networks")) return "set wifi networks " + c["networks"].dump();
    return "set wifi";
  }
  if (t == "ble_host") {
    if (c.value("action", "connect") == "disconnect") return "set ble disconnect";
    return "set ble connect " + c.value("name", "Phone");
  }
  if (t == "ble_bonds") return "set ble full " + onOff(c.value("full", true));
  if (t == "power_switch") return "set switch " + onOff(c.value("on", true));
  if (t == "restart") return c.value("cold_boot", true) ? "restart --cold" : "restart --wake";
  if (t == "import") return "import " + c.value("file", "");
  if (t == "dump") return "shot " + c.value("path", "");
  return "cmd " + c.dump();
}

bool parseWords(const std::vector<std::string>& w, json& c, std::string& err) {
  c = json::object();
  auto need = [&](size_t n) {
    if (w.size() < n) {
      err = "missing value - see sim set --help";
      return false;
    }
    return true;
  };
  if (w.empty()) {
    err = "nothing to do";
    return false;
  }
  const std::string v = w[0];
  if (v == "press" || v == "down" || v == "up" || v == "hold") {
    if (!need(2)) return false;
    c = {{"type", "button"}, {"button", up(w[1])}, {"action", v}};
    if (v == "hold" && w.size() > 2) c["duration_ms"] = std::atoi(w[2].c_str());
    return true;
  }
  if (v == "wait") {
    if (!need(2) || !isNum(w[1])) {
      err = "wait MS";
      return false;
    }
    c = {{"type", "wait"}, {"ms", std::atoi(w[1].c_str())}};
    return true;
  }
  if (v == "restart") {
    const bool wake = w.size() > 1 && w[1] == "--wake";
    c = {{"type", "restart"}, {"cold_boot", !wake}};
    return true;
  }
  if (v == "import") {
    if (!need(2)) return false;
    c = {{"type", "import"}, {"file", w[1]}};
    return true;
  }
  if (v != "set" || !need(3)) {
    if (v != "set") err = "unknown command '" + v + "'";
    return false;
  }
  const std::string what = w[1], a = w[2];
  if (what == "battery") {
    c = {{"type", "battery"}};
    if (a == "unknown") c["percent"] = "unknown";
    else if (isNum(a)) c["percent"] = std::atoi(a.c_str());
    else return err = "set battery 0-100|unknown", false;
  } else if (what == "usb") {
    c = {{"type", "usb"}, {"plugged", a == "unknown" ? json("unknown") : json(a == "on")}};
  } else if (what == "sd") {
    if (a == "fail") {
      if (!need(4)) return false;
      c = {{"type", "storage"}, {"fail_writes", w[3] == "on"}};
    } else {
      c = {{"type", "sd"}, {"present", a == "in"}};
    }
  } else if (what == "rtc") {
    c = {{"type", "rtc"}};
    if (a == "missing") c["missing"] = true;
    else c["time"] = a;
  } else if (what == "ir") {
    if (a == "raw") {
      if (!need(4)) return false;
      c = {{"type", "ir_signal"}, {"protocol", "RAW"}, {"raw_file", w[3]}};
    } else {
      if (!need(5)) return false;
      c = {{"type", "ir_signal"}, {"protocol", a}, {"address", w[3]}, {"command", w[4]}};
    }
  } else if (what == "nfc") {
    if (a == "remove") c = {{"type", "nfc_card"}, {"action", "remove"}};
    else if (a == "reader") c = {{"type", "nfc_reader"}};
    else if (a == "module") c = {{"type", "nfc_module"}, {"present", w.size() < 4 || w[3] == "on"}};
    else if (a == "place") {
      if (!need(4)) return false;
      c = {{"type", "nfc_card"}, {"action", "present"}, {"uid", w[3]}, {"card_type", "NTAG215"}};
      for (size_t i = 4; i < w.size(); i++) {
        if (w[i] == "classic") {
          c["card_type"] = "MIFARE Classic 1K";
          json blocks = json::array();
          for (int b = 0; b < 64; b++) blocks.push_back(b == 0 ? (w[3] + std::string(32, '0')).substr(0, 32) : std::string(32, '0'));
          c["blocks"] = blocks;
        } else if (w[i] == "ntag") {
          c["card_type"] = "NTAG215";
        } else if (w[i] == "magic") {
          c["magic"] = true;
        } else {
          c["card_type"] = w[i];
        }
      }
    } else return err = "set nfc place|remove|reader|module", false;
  } else if (what == "wifi") {
    c = {{"type", "wifi"}};
    if (a == "next" && need(4)) c["next_connect_succeeds"] = w[3] == "ok";
    else if (a == "ntp" && need(4)) c["ntp_responds"] = w[3] == "on";
    else if (a == "delay" && need(4)) c["latency_ms"] = std::atoi(w[3].c_str());
    else if (a == "drop") c["drop"] = true;
    else if (a == "networks" && need(4)) c["networks"] = json::parse(w[3], nullptr, false);
    else return err = "set wifi next|ntp|delay|drop|networks", false;
  } else if (what == "ble") {
    if (a == "connect") c = {{"type", "ble_host"}, {"action", "connect"}, {"name", w.size() > 3 ? w[3] : "Phone"}};
    else if (a == "disconnect") c = {{"type", "ble_host"}, {"action", "disconnect"}};
    else if (a == "full") c = {{"type", "ble_bonds"}, {"full", w.size() < 4 || w[3] == "on"}};
    else return err = "set ble connect NAME|disconnect|full on|off", false;
  } else if (what == "switch") {
    c = {{"type", "power_switch"}, {"on", a == "on"}};
  } else {
    err = "unknown thing to set '" + what + "' - see sim set --help";
    return false;
  }
  return true;
}

}  // namespace simcli

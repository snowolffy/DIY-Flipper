// wifi.cpp - the WiFi-new flow: F0 menu, C1 scanning, C2 networks, C4 password, C5 connecting,
// C6a connected (saves the password, NTP -> DS3231), C6b failed, N1 saved networks, N1d forget.
// WiFi is manual only: nothing connects at boot and leaving these pages doesn't disconnect.
#include <algorithm>

#include "app/modules.h"
#include "app/widgets.h"

namespace app {
namespace wifi {

namespace {

constexpr const char* kSavedPath = "/wifi.ini";  // ssid<TAB>password per line, plain text in flash
constexpr uint32_t kConnectTimeoutMs = 15000;
constexpr uint32_t kConnectedMs = 1500;

struct Saved {
  std::string ssid, password;
};

std::vector<Saved> loadSaved(App& app) {
  std::vector<Saved> out;
  std::string text;
  if (!app.hal().storage.read(hal::Volume::Flash, kSavedPath, text)) return out;
  size_t p = 0;
  while (p < text.size()) {
    size_t e = text.find('\n', p);
    if (e == std::string::npos) e = text.size();
    std::string line = text.substr(p, e - p);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const size_t tab = line.find('\t');
    if (tab != std::string::npos) out.push_back({line.substr(0, tab), line.substr(tab + 1)});
    p = e + 1;
  }
  return out;
}

void writeSaved(App& app, const std::vector<Saved>& list) {
  std::string t;
  for (const auto& s : list) t += s.ssid + "\t" + s.password + "\n";
  app.hal().storage.write(hal::Volume::Flash, kSavedPath, t);
}

const Saved* findSaved(const std::vector<Saved>& list, const std::string& ssid) {
  for (const auto& s : list)
    if (s.ssid == ssid) return &s;
  return nullptr;
}

int level(int rssi) { return rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : rssi >= -85 ? 1 : 0; }

std::unique_ptr<Screen> makeScanning();
std::unique_ptr<Screen> makePassword(const std::string& ssid);

void backToNetworks(App& app) {
  if (app.find("C2") >= 0) app.popTo("C2");
  else app.popTo("N1");
}

// ---- C5 connecting / C6 result ----
std::unique_ptr<Screen> makeConnected(const std::string& ssid, const std::string& password) {
  auto p = std::make_unique<PageScreen>("C6a", "", "Connected");
  PageScreen* raw = p.get();
  p->icon("check_new").lines({"CONNECTED", "TIME SYNCED"}).noBottom();
  (void)raw;
  p->tick([ssid, password, done = false](App& app, PageScreen& s) mutable {
    if (done) return;
    done = true;
    // the password is saved only now that it worked
    auto list = loadSaved(app);
    list.erase(std::remove_if(list.begin(), list.end(), [&](const Saved& x) { return x.ssid == ssid; }), list.end());
    list.insert(list.begin(), {ssid, password});
    writeSaved(app, list);
    // network time -> DS3231 (local time, fixed UTC+7)
    hal::DateTime utc;
    if (app.hal().wifi.ntpTime(utc)) {
      int h = utc.hour + App::kTimeZoneHours;
      hal::DateTime local = utc;
      if (h >= 24) {
        h -= 24;
        static const int dim[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        int days = dim[local.month - 1] + (local.month == 2 && local.year % 4 == 0 ? 1 : 0);
        if (++local.day > days) {
          local.day = 1;
          if (++local.month > 12) local.month = 1, local.year++;
        }
      }
      local.hour = (uint8_t)h;
      app.hal().rtc.set(local);
    } else {
      s.textLines()[1] = "NO TIME SYNC";
    }
    app.beep(2600, 60);
  });
  p->timer(kConnectedMs, [](App& app) { app.popTo("F0"); });
  return p;
}

std::unique_ptr<Screen> makeFailed(const std::string& ssid, bool secured) {
  auto p = std::make_unique<PageScreen>("C6b", "", "Failed");
  p->lines({secured ? "WRONG PASSWORD" : "CONNECT FAILED"}).linesY(64).hint("OK=RETRY  CANCEL=BACK");
  p->onOk([ssid, secured](App& app) {
    if (secured) app.replace(makePassword(ssid));
    else app.pop();
  });
  p->onCancel([](App& app) { backToNetworks(app); });
  return p;
}

std::unique_ptr<Screen> makeConnecting(const std::string& ssid, const std::string& password, bool secured) {
  auto p = std::make_unique<PageScreen>("C5", "WIFI", "Connecting");
  p->icon("wifi").busy().lines({"CONNECTING TO", tk::upper(ssid)}).noBottom();
  p->tick([ssid, password, started = false](App& app, PageScreen&) mutable {
    if (!started) {
      started = true;
      app.hal().wifi.connect(ssid, password);
    }
  });
  p->timer(kConnectTimeoutMs, [](App& app) {
    app.hal().wifi.disconnect();
    app.emit(SysEvent::WifiConnectFailed);
  });
  p->system([ssid, password, secured](App& app, SysEvent e) {
    if (e == SysEvent::WifiConnected) app.replace(makeConnected(ssid, password));
    else if (e == SysEvent::WifiConnectFailed) app.replace(makeFailed(ssid, secured));
  });
  p->onCancel([](App& app) {
    app.hal().wifi.disconnect();
    backToNetworks(app);
  });
  return p;
}

std::unique_ptr<Screen> makePassword(const std::string& ssid) {
  auto t = std::make_unique<TextInputScreen>(
      "C4", "PASSWORD", TextInputScreen::kPasswordChars, "", 63,
      [ssid](App& app, const std::string& pw) { app.replace(makeConnecting(ssid, pw, true)); },
      [](App& app) { app.pop(); });
  t->showCase();
  return t;
}

// ---- C2 networks ----
std::unique_ptr<Screen> makeNetworks() {
  auto m = std::make_unique<MenuScreen>("C2", "NETWORKS", [](App& app) {
    std::vector<MenuItem> items;
    const auto saved = loadSaved(app);
    const std::string current = app.hal().wifi.connectedSsid();
    for (const auto& n : app.hal().wifi.scanResults()) {
      MenuItem it;
      it.row.label = tk::upper(n.ssid);
      it.row.lock = n.secured;
      it.row.signal = level(n.rssi);
      if (n.ssid == current || findSaved(saved, n.ssid)) it.row.trail = tk::Trail::Check;
      const std::string ssid = n.ssid;
      const bool secured = n.secured;
      it.onOk = [ssid, secured](App& a) {
        const auto s = loadSaved(a);
        const Saved* known = findSaved(s, ssid);
        if (known || !secured) a.push(makeConnecting(ssid, known ? known->password : "", secured));
        else a.push(makePassword(ssid));
      };
      items.push_back(it);
    }
    MenuItem rescan;
    rescan.row.label = "RESCAN";
    rescan.onOk = [](App& a) { a.replace(makeScanning()); };
    items.push_back(rescan);
    return items;
  }, "Networks");
  m->onBack([](App& app) { app.popTo("F0"); });
  return m;
}

std::unique_ptr<Screen> makeScanning() {
  auto p = std::make_unique<PageScreen>("C1", "SCANNING", "Scanning");
  p->icon("wifi").busy().lines({"SCANNING..."}).noBottom();
  p->tick([started = false](App& app, PageScreen&) mutable {
    if (!started) {
      started = true;
      app.hal().wifi.startScan();
    }
  });
  p->system([](App& app, SysEvent e) {
    if (e == SysEvent::WifiScanDone) app.replace(makeNetworks());
  });
  p->onCancel([](App& app) { app.popTo("F0"); });
  return p;
}

// ---- N1 saved networks ----
std::unique_ptr<Screen> makeSavedList() {
  auto m = std::make_unique<MenuScreen>("N1", "SAVED NETWORKS", [](App& app) {
    std::vector<MenuItem> items;
    const std::string current = app.hal().wifi.connectedSsid();
    for (const auto& s : loadSaved(app)) {
      MenuItem it;
      it.row.label = tk::upper(s.ssid);
      if (s.ssid == current) it.row.trail = tk::Trail::Check;
      const Saved sv = s;
      it.onOk = [sv](App& a) { a.push(makeConnecting(sv.ssid, sv.password, true)); };
      it.onManage = [sv](App& a) {
        a.push(std::make_unique<DialogScreen>(
                   "N1d", std::vector<std::string>{"FORGET", tk::upper(sv.ssid) + "?"},
                   [sv](App& b) {
                     // forgetting doesn't drop the current connection
                     auto list = loadSaved(b);
                     list.erase(std::remove_if(list.begin(), list.end(), [&](const Saved& x) { return x.ssid == sv.ssid; }),
                                list.end());
                     writeSaved(b, list);
                     b.pop();
                   }),
               fade(150));
      };
      items.push_back(it);
    }
    return items;
  }, "Saved networks");
  m->empty("NO SAVED NETWORKS");
  return m;
}

}  // namespace

std::unique_ptr<Screen> makeMenu(App&) {
  return std::make_unique<MenuScreen>("F0", "WIFI", [](App& app) {
    std::vector<MenuItem> items(2);
    items[0].row.label = "CONNECT", items[0].onOk = [](App& a) { a.push(makeScanning()); };
    items[1].row.label = "SAVED NETWORKS", items[1].onOk = [](App& a) { a.push(makeSavedList()); };
    if (app.hal().wifi.state() == hal::WifiState::Connected) {
      MenuItem d;
      d.row.label = "DISCONNECT";
      d.onOk = [](App& a) {
        a.hal().wifi.disconnect();
        a.toast("DISCONNECTED");
      };
      items.push_back(d);
    }
    return items;
  }, "WiFi");
}

}  // namespace wifi
}  // namespace app

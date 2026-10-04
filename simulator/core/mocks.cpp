#include "core/mocks.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include "app/battery.h"
#include "board/board_profile.h"
#include "miniz/miniz.h"

namespace fs = std::filesystem;

namespace sim {

const char* wifiStateName(hal::WifiState s) {
  switch (s) {
    case hal::WifiState::Off: return "off";
    case hal::WifiState::Idle: return "idle";
    case hal::WifiState::Scanning: return "scanning";
    case hal::WifiState::Connecting: return "connecting";
    case hal::WifiState::Connected: return "connected";
    case hal::WifiState::Failed: return "failed";
  }
  return "?";
}

const char* bleStateName(hal::BleState s) {
  switch (s) {
    case hal::BleState::Off: return "off";
    case hal::BleState::Advertising: return "advertising";
    case hal::BleState::PairingRequest: return "pairing_request";
    case hal::BleState::Connected: return "connected";
  }
  return "?";
}

const char* powerStateName(PowerState s) {
  switch (s) {
    case PowerState::Awake: return "awake";
    case PowerState::LightSleep: return "light_sleep";
    case PowerState::DeepSleep: return "deep_sleep";
    case PowerState::Off: return "off";
  }
  return "?";
}

// ---------------- display ----------------

void MockDisplay::push(const uint16_t* frame, int16_t x, int16_t y, int16_t w, int16_t h) {
  // only the window is sent, as on the device
  for (int16_t yy = y; yy < y + h; yy++)
    std::memcpy(frame_ + yy * ui::kScreenW + x, frame + yy * ui::kScreenW + x, (size_t)w * 2);
  pushes_++;
  const uint64_t bytes = (uint64_t)w * h * 2;
  bytes_ += bytes;
  lastMs_ = (double)bytes * 8 * 1000 / board::kDisplaySpiHz;
  if (clock_) {
    pushTimes_.push_back(clock_->millis());
    while (pushTimes_.size() > 64) pushTimes_.pop_front();
  }
}

int MockDisplay::fps(uint32_t now) const {
  int n = 0;
  for (uint32_t t : pushTimes_)
    if (now - t < 1000) n++;
  return n;
}

std::string MockDisplay::hash() const {
  uint64_t h = 1469598103934665603ull;
  for (size_t i = 0; i < ui::kPixels; i++) {
    const uint8_t b[2] = {(uint8_t)(frame_[i] & 0xFF), (uint8_t)(frame_[i] >> 8)};
    for (uint8_t v : b) {
      h ^= v;
      h *= 1099511628211ull;
    }
  }
  char out[17];
  std::snprintf(out, sizeof(out), "%016llx", (unsigned long long)h);
  return out;
}

std::vector<uint8_t> MockDisplay::png(int scale) const {
  if (scale < 1) scale = 1;
  const int w = ui::kScreenW * scale, h = ui::kScreenH * scale;
  std::vector<uint8_t> rgb((size_t)w * h * 3);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      const uint16_t c = frame_[(y / scale) * ui::kScreenW + x / scale];
      uint8_t* p = &rgb[((size_t)y * w + x) * 3];
      // 565 -> 888 with the low bits filled from the high ones (white stays 255)
      const uint8_t r = (uint8_t)((c >> 11) & 31), g = (uint8_t)((c >> 5) & 63), b = (uint8_t)(c & 31);
      p[0] = (uint8_t)(r << 3 | r >> 2);
      p[1] = (uint8_t)(g << 2 | g >> 4);
      p[2] = (uint8_t)(b << 3 | b >> 2);
    }
  size_t len = 0;
  void* data = tdefl_write_image_to_png_file_in_memory(rgb.data(), w, h, 3, &len);
  std::vector<uint8_t> out;
  if (data) {
    out.assign((uint8_t*)data, (uint8_t*)data + len);
    mz_free(data);
  }
  return out;
}

bool MockDisplay::writePng(const fs::path& path, int scale) const {
  const std::vector<uint8_t> data = png(scale);
  if (data.empty()) return false;
  std::ofstream f(path, std::ios::binary);
  f.write((const char*)data.data(), (std::streamsize)data.size());
  return (bool)f;
}

// ---------------- storage ----------------

bool MockStorage::parse(const std::string& spec, hal::Volume& v, std::string& path) {
  const size_t colon = spec.find(':');
  if (colon == std::string::npos) return false;
  const std::string vol = spec.substr(0, colon);
  if (vol == "sd") v = hal::Volume::Sd;
  else if (vol == "flash") v = hal::Volume::Flash;
  else return false;
  path = spec.substr(colon + 1);
  return !path.empty() && path[0] == '/';
}

fs::path MockStorage::hostPath(hal::Volume v, const std::string& path) const {
  fs::path p = root_ / (v == hal::Volume::Sd ? "sd" : "flash");
  // strip the leading slash and refuse to leave the volume folder
  fs::path rel = fs::path(path).relative_path().lexically_normal();
  if (!rel.empty() && *rel.begin() == "..") return {};
  return p / rel;
}

bool MockStorage::present(hal::Volume v) const { return v == hal::Volume::Flash || sdPresent_; }

bool MockStorage::exists(hal::Volume v, const std::string& path) const {
  if (!present(v)) return false;
  const fs::path p = hostPath(v, path);
  std::error_code ec;
  return !p.empty() && fs::exists(p, ec);
}

bool MockStorage::read(hal::Volume v, const std::string& path, std::string& out) const {
  if (!present(v)) return false;
  const fs::path p = hostPath(v, path);
  if (p.empty()) return false;
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

bool MockStorage::write(hal::Volume v, const std::string& path, const std::string& data) {
  if (!present(v) || failWrites_) return false;
  const fs::path p = hostPath(v, path);
  if (p.empty()) return false;
  std::error_code ec;
  fs::create_directories(p.parent_path(), ec);
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f << data;
  return (bool)f;
}

namespace {

bool listEntries(const fs::path& p, bool dirs, std::vector<std::string>& names) {
  names.clear();
  std::error_code ec;
  if (p.empty() || !fs::is_directory(p, ec)) return false;
  for (const auto& e : fs::directory_iterator(p, ec)) {
    const std::string name = e.path().filename().string();
    const bool match = dirs ? e.is_directory(ec) : e.is_regular_file(ec);
    if (match && name != ".gitkeep") names.push_back(name);
  }
  std::sort(names.begin(), names.end());
  return true;
}

}  // namespace

bool MockStorage::list(hal::Volume v, const std::string& dir, std::vector<std::string>& names) const {
  names.clear();
  return present(v) && listEntries(hostPath(v, dir), false, names);
}

bool MockStorage::listDirs(hal::Volume v, const std::string& dir, std::vector<std::string>& names) const {
  names.clear();
  return present(v) && listEntries(hostPath(v, dir), true, names);
}

bool MockStorage::remove(hal::Volume v, const std::string& path) {
  if (!present(v) || failWrites_) return false;
  const fs::path p = hostPath(v, path);
  std::error_code ec;
  if (p.empty() || !fs::exists(p, ec)) return false;
  fs::remove_all(p, ec);
  return !ec;
}

bool MockStorage::rename(hal::Volume v, const std::string& from, const std::string& to) {
  if (!present(v) || failWrites_) return false;
  const fs::path a = hostPath(v, from), b = hostPath(v, to);
  std::error_code ec;
  if (a.empty() || b.empty() || !fs::exists(a, ec) || fs::exists(b, ec)) return false;
  fs::create_directories(b.parent_path(), ec);
  fs::rename(a, b, ec);
  return !ec;
}

bool MockStorage::usage(hal::Volume v, uint64_t& used, uint64_t& total) const {
  if (!present(v)) return false;
  used = 0;
  total = v == hal::Volume::Sd ? kSdTotal : kFlashTotal;
  std::error_code ec;
  const fs::path p = root_ / (v == hal::Volume::Sd ? "sd" : "flash");
  if (fs::is_directory(p, ec))
    for (const auto& e : fs::recursive_directory_iterator(p, ec))
      if (e.is_regular_file(ec)) used += e.file_size(ec);
  return true;
}

// ---------------- battery ----------------

void MockBattery::setPercent(int percent) {
  pct_ = percent;
  mv_ = app::batteryMvFromPercent(percent);
}

// ---------------- rtc ----------------

namespace {

// Howard Hinnant's days_from_civil / civil_from_days
int64_t daysFromCivil(int64_t y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int64_t)doe - 719468;
}

void civilFromDays(int64_t z, int64_t& y, unsigned& m, unsigned& d) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = (unsigned)(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  y = (int64_t)yoe + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp < 10 ? mp + 3 : mp - 9;
  y += m <= 2;
}

}  // namespace

int64_t MockRtc::epoch(const hal::DateTime& t) {
  return daysFromCivil(t.year, t.month, t.day) * 86400 + t.hour * 3600 + t.minute * 60 + t.second;
}

hal::DateTime MockRtc::fromEpoch(int64_t t) {
  const int64_t days = t >= 0 ? t / 86400 : (t - 86399) / 86400;
  const int64_t secs = t - days * 86400;
  int64_t y;
  unsigned m, d;
  civilFromDays(days, y, m, d);
  hal::DateTime out;
  out.year = (uint16_t)y;
  out.month = (uint8_t)m;
  out.day = (uint8_t)d;
  out.hour = (uint8_t)(secs / 3600);
  out.minute = (uint8_t)(secs % 3600 / 60);
  out.second = (uint8_t)(secs % 60);
  return out;
}

bool MockRtc::now(hal::DateTime& out) {
  if (missing_) return false;
  out = fromEpoch(baseEpoch_ + (int64_t)((clock_.millis() - baseMillis_) / 1000));
  return true;
}

bool MockRtc::set(const hal::DateTime& t) {
  baseEpoch_ = epoch(t);
  baseMillis_ = clock_.millis();
  missing_ = false;
  return true;
}

bool MockRtc::parse(const std::string& iso, hal::DateTime& out) {
  unsigned y, mo, d, h = 0, mi = 0, s = 0;
  const int n = std::sscanf(iso.c_str(), "%u-%u-%uT%u:%u:%u", &y, &mo, &d, &h, &mi, &s);
  if (n < 3 || mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 59) return false;
  out.year = (uint16_t)y;
  out.month = (uint8_t)mo;
  out.day = (uint8_t)d;
  out.hour = (uint8_t)h;
  out.minute = (uint8_t)mi;
  out.second = (uint8_t)s;
  return true;
}

std::string MockRtc::format(const hal::DateTime& t) {
  char b[24];
  std::snprintf(b, sizeof(b), "%04u-%02u-%02uT%02u:%02u:%02u", t.year, t.month, t.day, t.hour, t.minute, t.second);
  return b;
}

// ---------------- buzzer ----------------

void MockBuzzer::tone(uint16_t hz, uint16_t ms) {
  hz_ = hz;
  until_ = clock_.millis() + ms;
  history_.push_back({clock_.millis(), hz, ms});
  if (history_.size() > 20) history_.pop_front();
  count_++;
}

// ---------------- IR ----------------

void MockIr::inject(const hal::IrSignal& s) {
  if (listening_) inbox_.push_back(s);
}

bool MockIr::receive(hal::IrSignal& out) {
  if (!listening_ || inbox_.empty()) return false;
  out = inbox_.front();
  inbox_.pop_front();
  return true;
}

bool MockIr::send(const hal::IrSignal& s) {
  sent_.push_back(s);
  if (sent_.size() > 20) sent_.pop_front();
  sentCount_++;
  return true;
}

bool MockIr::parseRaw(const std::string& text, std::vector<uint16_t>& out) {
  out.clear();
  std::string tok;
  auto flush = [&] {
    if (tok.empty()) return true;
    char* end = nullptr;
    const unsigned long v = std::strtoul(tok.c_str(), &end, 10);
    tok.clear();
    if (*end || v == 0 || v > 65535) return false;
    out.push_back((uint16_t)v);
    return true;
  };
  for (char c : text) {
    if (c == ' ' || c == ',' || c == '\n' || c == '\r' || c == '\t') {
      if (!flush()) return false;
    } else if (c == '#') {
      break;
    } else {
      tok += c;
    }
  }
  return flush() && out.size() >= 2;
}

// ---------------- NFC ----------------

bool MockNfc::card(hal::NfcCard& out) {
  if (!modulePresent_ || !polling_ || !present_) return false;
  out = card_;
  return true;
}

bool MockNfc::writeBlock(int index, const std::string& hex) {
  if (!modulePresent_ || !present_) return false;
  if (index == 0 && !card_.magic) return false;  // the UID block of a normal card is read-only
  if (index < 0) return false;
  if ((size_t)index >= card_.blocks.size()) card_.blocks.resize((size_t)index + 1);
  card_.blocks[(size_t)index] = hex;
  if (index == 0 && hex.size() >= 8) card_.uid = hex.substr(0, card_.uid.size() ? card_.uid.size() : 8);
  written_++;
  return true;
}

void MockNfc::startEmulation(const hal::NfcCard& c) {
  emu_ = c;
  emulating_ = true;
  taps_ = 0;
}

// ---------------- WiFi ----------------

void MockWifi::startScan() {
  state_ = hal::WifiState::Scanning;
  doneAt_ = clock_.millis() + latencyMs_;
}

void MockWifi::connect(const std::string& ssid, const std::string& password) {
  state_ = hal::WifiState::Connecting;
  ssid_ = ssid;
  password_ = password;
  doneAt_ = clock_.millis() + latencyMs_;
}

void MockWifi::disconnect() {
  state_ = hal::WifiState::Off;
  ssid_.clear();
}

bool MockWifi::ntpTime(hal::DateTime& utc) {
  if (state_ != hal::WifiState::Connected || !ntp_) return false;
  utc = MockRtc::fromEpoch(worldEpoch_ + clock_.millis() / 1000);
  return true;
}

void MockWifi::tick() {
  if (state_ != hal::WifiState::Scanning && state_ != hal::WifiState::Connecting) return;
  if ((int32_t)(clock_.millis() - doneAt_) < 0) return;
  if (state_ == hal::WifiState::Scanning) {
    results_ = networks_;
    std::stable_sort(results_.begin(), results_.end(),
                     [](const hal::WifiNetwork& a, const hal::WifiNetwork& b) { return a.rssi > b.rssi; });
    state_ = hal::WifiState::Idle;
  } else {
    const bool known = std::any_of(networks_.begin(), networks_.end(),
                                   [&](const hal::WifiNetwork& n) { return n.ssid == ssid_; });
    state_ = known && nextOk_ ? hal::WifiState::Connected : hal::WifiState::Failed;
  }
}

// ---------------- BLE ----------------

void MockBle::startAdvertising(const std::string& deviceName) {
  advName_ = deviceName;
  if (state_ == hal::BleState::Off) {
    host_.clear();
    state_ = hal::BleState::Advertising;
  }
}

void MockBle::stop() {
  host_.clear();
  state_ = hal::BleState::Off;
}

void MockBle::disconnect() {
  if (state_ == hal::BleState::Off) return;
  host_.clear();
  state_ = hal::BleState::Advertising;
}

void MockBle::hostConnect(const std::string& name) {
  if (state_ != hal::BleState::Advertising) return;  // nothing to connect to
  host_ = name;
  auto it = std::find(bonded_.begin(), bonded_.end(), name);
  if (it != bonded_.end()) {
    bonded_.erase(it);
    bonded_.insert(bonded_.begin(), name);  // most recently used first
    state_ = hal::BleState::Connected;
  } else {
    // deterministic six-digit code so screenshots and hashes repeat
    passkey_ = (123456u + 271828u * pairings_++) % 1000000u;
    state_ = hal::BleState::PairingRequest;
  }
}

void MockBle::hostDisconnect() {
  if (state_ == hal::BleState::Connected || state_ == hal::BleState::PairingRequest) {
    host_.clear();
    state_ = hal::BleState::Advertising;
  }
}

void MockBle::confirmPairing(bool accept) {
  if (state_ != hal::BleState::PairingRequest) return;
  if (accept) {
    if (bonded_.size() >= kMaxBonds) bonded_.pop_back();  // drop the least recently used
    bonded_.insert(bonded_.begin(), host_);
    state_ = hal::BleState::Connected;
  } else {
    host_.clear();
    state_ = hal::BleState::Advertising;
  }
}

std::vector<hal::BleBond> MockBle::bonds() const {
  std::vector<hal::BleBond> out;
  for (const std::string& n : bonded_) {
    // a stable made-up address per name
    uint32_t h = 2166136261u;
    for (char c : n) h = (h ^ (uint8_t)c) * 16777619u;
    char a[18];
    std::snprintf(a, sizeof(a), "AA:BB:CC:%02X:%02X:%02X", (h >> 16) & 255, (h >> 8) & 255, h & 255);
    out.push_back({n, a});
  }
  return out;
}

void MockBle::forget(const std::string& host) {
  bonded_.erase(std::remove(bonded_.begin(), bonded_.end(), host), bonded_.end());
  if (state_ == hal::BleState::Connected && host_ == host) disconnect();
}

bool MockBle::sendKey(const hal::HidKey& key) {
  if (state_ != hal::BleState::Connected) return false;
  keys_.push_back(key);
  if (keys_.size() > 20) keys_.pop_front();
  keysSent_++;
  return true;
}

void MockBle::setBondListFull(bool full) {
  if (full) {
    for (int i = 1; bonded_.size() < kMaxBonds; i++) bonded_.push_back("Old device " + std::to_string(i));
  } else {
    bonded_.erase(std::remove_if(bonded_.begin(), bonded_.end(),
                                 [](const std::string& n) { return n.rfind("Old device ", 0) == 0; }),
                  bonded_.end());
  }
}

}  // namespace sim

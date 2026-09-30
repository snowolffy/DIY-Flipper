#include "core/mocks.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include "app/battery.h"

namespace fs = std::filesystem;

namespace sim {

// ---------------- display ----------------

void MockDisplay::push(const uint8_t* frame, bool invert) {
  std::memcpy(frame_, frame, sizeof(frame_));
  invert_ = invert;
  pushes_++;
}

bool MockDisplay::lit(int16_t x, int16_t y) const {
  const uint32_t idx = (uint32_t)y * ui::kScreenW + x;
  const bool ink = (frame_[idx >> 3] >> (idx & 7)) & 1;
  return ink != invert_;
}

std::string MockDisplay::hash() const {
  uint64_t h = 1469598103934665603ull;
  for (size_t i = 0; i < sizeof(frame_); i++) {
    const uint8_t b = invert_ ? (uint8_t)~frame_[i] : frame_[i];
    h ^= b;
    h *= 1099511628211ull;
  }
  char out[17];
  std::snprintf(out, sizeof(out), "%016llx", (unsigned long long)h);
  return out;
}

bool MockDisplay::writePbm(const fs::path& path) const {
  std::ofstream f(path, std::ios::binary);
  if (!f) return false;
  // P4: 1 = black, rows padded to bytes, MSB first. Lit pixels are written black so the image reads like
  // the screen's ink on paper.
  f << "P4\n" << ui::kScreenW << " " << ui::kScreenH << "\n";
  for (int16_t y = 0; y < ui::kScreenH; y++) {
    for (int16_t x = 0; x < ui::kScreenW; x += 8) {
      uint8_t byte = 0;
      for (int k = 0; k < 8; k++)
        if (lit(x + k, y)) byte |= (uint8_t)(0x80 >> k);
      f.put((char)byte);
    }
  }
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

// ---------------- battery ----------------

void MockBattery::setPercent(int percent) { mv_ = app::batteryMvFromPercent(percent); }

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

bool MockRtc::now(hal::DateTime& out) {
  if (missing_) return false;
  const int64_t t = baseEpoch_ + (int64_t)((clock_.millis() - baseMillis_) / 1000);
  const int64_t days = t >= 0 ? t / 86400 : (t - 86399) / 86400;
  const int64_t secs = t - days * 86400;
  int64_t y;
  unsigned m, d;
  civilFromDays(days, y, m, d);
  out.year = (uint16_t)y;
  out.month = (uint8_t)m;
  out.day = (uint8_t)d;
  out.hour = (uint8_t)(secs / 3600);
  out.minute = (uint8_t)(secs % 3600 / 60);
  out.second = (uint8_t)(secs % 60);
  return true;
}

void MockRtc::set(const hal::DateTime& t) {
  baseEpoch_ = daysFromCivil(t.year, t.month, t.day) * 86400 + t.hour * 3600 + t.minute * 60 + t.second;
  baseMillis_ = clock_.millis();
  missing_ = false;
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

// ---------------- NFC ----------------

bool MockNfc::card(hal::NfcCard& out) {
  if (!polling_ || !present_) return false;
  out = card_;
  return true;
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

void MockWifi::tick() {
  if (state_ != hal::WifiState::Scanning && state_ != hal::WifiState::Connecting) return;
  if ((int32_t)(clock_.millis() - doneAt_) < 0) return;
  if (state_ == hal::WifiState::Scanning) {
    results_ = networks_;
    std::sort(results_.begin(), results_.end(),
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
  host_.clear();
  state_ = hal::BleState::Advertising;
}

void MockBle::hostConnect(const std::string& name) {
  if (state_ != hal::BleState::Advertising) return;  // nothing to connect to
  host_ = name;
  if (std::find(bonded_.begin(), bonded_.end(), name) != bonded_.end()) {
    state_ = hal::BleState::Connected;
  } else if (bonded_.size() >= kMaxBonds) {
    state_ = hal::BleState::BondListFull;
  } else {
    // deterministic six-digit code so screenshots and hashes repeat
    passkey_ = (123456u + 271828u * pairings_++) % 1000000u;
    state_ = hal::BleState::PairingRequest;
  }
}

void MockBle::hostDisconnect() {
  if (state_ == hal::BleState::Off) return;
  host_.clear();
  state_ = hal::BleState::Advertising;
}

void MockBle::confirmPairing(bool accept) {
  if (state_ != hal::BleState::PairingRequest) return;
  if (accept) {
    bonded_.push_back(host_);
    state_ = hal::BleState::Connected;
  } else {
    host_.clear();
    state_ = hal::BleState::Advertising;
  }
}

bool MockBle::sendKey(uint16_t usage) {
  if (state_ != hal::BleState::Connected) return false;
  keys_.push_back(usage);
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

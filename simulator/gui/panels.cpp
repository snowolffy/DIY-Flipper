#include "gui/panels.h"

#include <algorithm>
#include <cstdio>

#include "app/minijson.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

namespace fs = std::filesystem;

namespace gui {

namespace {

const char* kIrProtocols[] = {"NEC", "Samsung", "Sony", "RC5"};
const char* kCardTypes[] = {"NTAG215", "NTAG213", "MIFARE Classic 1K", "MIFARE Ultralight"};

void hint(const char* text) {
  ImGui::SameLine();
  ImGui::TextDisabled("(?)");
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text);
}

void status(const char* label, const std::string& value, bool good) {
  ImGui::TextUnformatted(label);
  ImGui::SameLine();
  ImGui::TextColored(good ? ImVec4(0.45f, 0.85f, 0.55f, 1) : ImVec4(0.75f, 0.75f, 0.78f, 1), "%s", value.c_str());
}

bool inputHex(const char* label, unsigned& v) {
  ImGui::SetNextItemWidth(90);
  return ImGui::InputScalar(label, ImGuiDataType_U32, &v, nullptr, nullptr, "%02X", ImGuiInputTextFlags_CharsHexadecimal);
}

// Deterministic fake page data so a crafted card looks like a real read.
std::vector<std::string> fakeBlocks(const std::string& uid, int n) {
  std::vector<std::string> out;
  for (int i = 0; i < n; i++) {
    unsigned h = 2166136261u;
    for (char c : uid) h = (h ^ (unsigned char)c) * 16777619u;
    h = (h ^ (unsigned)i) * 16777619u;
    char b[12];
    std::snprintf(b, sizeof(b), "%08X", h);
    out.push_back(b);
  }
  return out;
}

const char* keyName(uint16_t k) {
  switch (k) {
    case 0xCD: return "Play/Pause";
    case 0xB5: return "Next";
    case 0xB6: return "Previous";
    case 0xE9: return "Volume +";
    case 0xEA: return "Volume -";
  }
  return "?";
}

}  // namespace

void drawIrPanel(sim::Simulator& s, RadioPanelState& st) {
  sim::MockIr& ir = s.ir();
  status("Receiver:", ir.listening() ? "listening (Learn screen open)" : "off", ir.listening());
  ImGui::SetNextItemWidth(120);
  ImGui::Combo("Protocol", &st.irProtocol, kIrProtocols, IM_ARRAYSIZE(kIrProtocols));
  inputHex("Address", st.irAddress);
  ImGui::SameLine();
  inputHex("Command", st.irCommand);
  if (ImGui::Button("Press remote button")) {
    hal::IrSignal sig;
    sig.protocol = kIrProtocols[st.irProtocol];
    sig.address = st.irAddress;
    sig.command = st.irCommand;
    ir.inject(sig);
  }
  hint("Like pointing a real remote at the IR eye: only received while the firmware is listening.");
  ImGui::Text("Sent by the device: %d", ir.sentCount());
  int shown = 0;
  for (auto it = ir.sent().rbegin(); it != ir.sent().rend() && shown < 4; ++it, ++shown)
    ImGui::BulletText("%s  addr 0x%02X  cmd 0x%02X", it->protocol.c_str(), (unsigned)it->address, (unsigned)it->command);
}

void drawNfcPanel(sim::Simulator& s, RadioPanelState& st) {
  sim::MockNfc& nfc = s.nfc();
  status("Reader:", nfc.polling() ? "polling (Read screen open)" : "idle", nfc.polling());
  ImGui::SameLine();
  status("  Card:", nfc.present() ? "in the field" : "none", nfc.present());

  ImGui::SetNextItemWidth(170);
  ImGui::InputText("UID (hex)", &st.nfcUid, ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase);
  ImGui::SetNextItemWidth(170);
  ImGui::Combo("Type", &st.nfcType, kCardTypes, IM_ARRAYSIZE(kCardTypes));
  ImGui::SetNextItemWidth(170);
  ImGui::SliderInt("Blocks", &st.nfcBlocks, 1, 64);

  // saved dumps on the SD card can be put back in the field
  std::vector<std::string> dumps;
  s.storage().list(hal::Volume::Sd, "/nfc", dumps);
  if (!dumps.empty()) {
    if (st.nfcDump >= (int)dumps.size()) st.nfcDump = 0;
    ImGui::SetNextItemWidth(170);
    if (ImGui::BeginCombo("Saved dump", dumps[st.nfcDump].c_str())) {
      for (int i = 0; i < (int)dumps.size(); i++)
        if (ImGui::Selectable(dumps[i].c_str(), i == st.nfcDump)) st.nfcDump = i;
      ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Use")) {
      std::string json;
      if (s.storage().read(hal::Volume::Sd, "/nfc/" + dumps[st.nfcDump], json)) {
        st.nfcUid = minijson::field(json, "uid");
        const std::string type = minijson::field(json, "type");
        for (int i = 0; i < IM_ARRAYSIZE(kCardTypes); i++)
          if (type == kCardTypes[i]) st.nfcType = i;
        st.nfcBlocks = std::max(1, (int)minijson::stringArray(json, "blocks").size());
      }
    }
  }

  if (ImGui::Button("Place card")) {
    hal::NfcCard c;
    c.uid = st.nfcUid.empty() ? "00000000" : st.nfcUid;
    c.type = kCardTypes[st.nfcType];
    c.blocks = fakeBlocks(c.uid, st.nfcBlocks);
    nfc.place(c);
  }
  ImGui::SameLine();
  if (ImGui::Button("Remove card")) nfc.remove();
}

void drawWifiPanel(sim::Simulator& s) {
  sim::MockWifi& w = s.wifi();
  static const char* names[] = {"off", "idle", "scanning", "connecting", "connected", "failed"};
  const hal::WifiState state = w.state();
  status("Radio:", names[(int)state] + (state == hal::WifiState::Connected ? " to " + w.connectedSsid() : std::string()),
         state == hal::WifiState::Connected);
  if (!w.lastPassword().empty()) ImGui::TextDisabled("Last password tried: %s", w.lastPassword().c_str());

  bool ok = w.nextConnectSucceeds();
  if (ImGui::Checkbox("Next connect succeeds", &ok)) w.setNextConnectSucceeds(ok);
  int latency = (int)w.latencyMs();
  ImGui::SetNextItemWidth(170);
  if (ImGui::SliderInt("Latency ms", &latency, 0, 5000)) w.setLatencyMs((uint32_t)latency);

  ImGui::TextUnformatted("Networks in range:");
  auto& nets = w.networks();
  int remove = -1;
  if (ImGui::BeginTable("nets", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerH)) {
    for (int i = 0; i < (int)nets.size(); i++) {
      ImGui::PushID(i);
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(130);
      ImGui::InputText("##ssid", &nets[i].ssid);
      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(90);
      ImGui::SliderInt("##rssi", &nets[i].rssi, -95, -30, "%d dBm");
      ImGui::TableNextColumn();
      ImGui::Checkbox("lock", &nets[i].secured);
      ImGui::TableNextColumn();
      if (ImGui::SmallButton("x")) remove = i;
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  if (remove >= 0) nets.erase(nets.begin() + remove);
  if (ImGui::Button("+ Add network")) nets.push_back({"New network", -60, true});
  hint("Changes show up on the next scan.");
}

void drawBlePanel(sim::Simulator& s, RadioPanelState& st) {
  sim::MockBle& b = s.ble();
  static const char* names[] = {"off", "advertising", "pairing request", "connected", "bond list full"};
  std::string line = names[(int)b.state()];
  if (b.state() == hal::BleState::Connected || b.state() == hal::BleState::PairingRequest) line += " - " + b.hostName();
  status("Radio:", line, b.state() == hal::BleState::Connected);
  if (b.state() == hal::BleState::PairingRequest) ImGui::Text("Host shows code %06u", (unsigned)b.passkey());

  ImGui::SetNextItemWidth(150);
  ImGui::InputText("Host name", &st.bleHost);
  ImGui::BeginDisabled(b.state() != hal::BleState::Advertising);
  if (ImGui::Button("Host connects")) b.hostConnect(st.bleHost);
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Host disconnects")) b.hostDisconnect();
  if (b.state() == hal::BleState::Off) ImGui::TextDisabled("Open Bluetooth Remote on the device to start advertising.");

  if (ImGui::Checkbox("Bond list full", &st.bondsFull)) b.setBondListFull(st.bondsFull);
  ImGui::TextUnformatted("Bonded hosts:");
  int forget = -1;
  for (int i = 0; i < (int)b.bonded().size(); i++) {
    ImGui::PushID(i);
    ImGui::BulletText("%s", b.bonded()[i].c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("forget")) forget = i;
    ImGui::PopID();
  }
  if (forget >= 0) b.bonded().erase(b.bonded().begin() + forget);

  ImGui::Text("Keys received by the host: %d", b.keysSent());
  int shown = 0;
  for (auto it = b.keys().rbegin(); it != b.keys().rend() && shown < 4; ++it, ++shown)
    ImGui::BulletText("%s", keyName(*it));
}

void drawScriptPanel(sim::Simulator& s, ScriptPanelState& st, const fs::path& project) {
  std::vector<fs::path> scripts;
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(project / "scripts", ec))
    if (e.path().extension() == ".json") scripts.push_back(e.path());
  std::sort(scripts.begin(), scripts.end());
  if (scripts.empty()) {
    ImGui::TextDisabled("No scripts in %s", (project / "scripts").string().c_str());
    return;
  }
  if (st.picked >= (int)scripts.size()) st.picked = 0;
  ImGui::SetNextItemWidth(220);
  if (ImGui::BeginCombo("##script", scripts[st.picked].filename().string().c_str())) {
    for (int i = 0; i < (int)scripts.size(); i++)
      if (ImGui::Selectable(scripts[i].filename().string().c_str(), i == st.picked)) st.picked = i;
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  if (ImGui::Button("Run here")) {
    sim::Script script;
    std::string err;
    if (!sim::loadScript(scripts[st.picked], script, err)) {
      st.message = err;
    } else if (!sim::copySeed(project, script.init.storageSeed, project / "storage", err)) {
      st.message = err;
    } else {
      sim::applyInitialState(script.init, s);
      if (st.restartFirst) s.restart(script.init.coldBoot);
      st.message = script.init.storageSeed.empty() ? "" : "Copied " + script.init.storageSeed + " into storage.";
      st.player.start(std::move(script), s.now());
    }
  }
  hint("Plays the script's events from now on. The seed folder is copied into this project's storage, "
       "so settings a script saves stay afterwards.");
  ImGui::Checkbox("Restart the device first", &st.restartFirst);
  hint("On: the device power-cycles (honouring the script's cold_boot) so the script runs exactly as it "
       "does headless. Off: the events play on top of whatever screen is open now.");
  if (st.player.active()) {
    ImGui::SameLine();
    if (ImGui::Button("Stop")) st.player.stop();
  }
  if (!st.message.empty()) ImGui::TextWrapped("%s", st.message.c_str());

  if (!st.player.active()) return;
  const auto& res = st.player.results();
  int failed = 0;
  for (const auto& r : res) failed += r.pass ? 0 : 1;
  ImGui::Text("%s: %s  (%d checks, %d failed)", st.player.script().name.c_str(),
              st.player.finished() ? "finished" : "running", (int)res.size(), failed);
  for (const auto& r : res) {
    ImGui::TextColored(r.pass ? ImVec4(0.45f, 0.85f, 0.55f, 1) : ImVec4(0.95f, 0.45f, 0.4f, 1), "%s t=%u %s",
                       r.pass ? "pass" : "FAIL", (unsigned)r.tMs, r.check.c_str());
    if (!r.pass) ImGui::TextWrapped("   %s", r.detail.c_str());
  }
}

}  // namespace gui

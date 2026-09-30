// panels.h - Mock control tabs for the radios and the script runner. Every control calls the same mock
// setter a script event does, so a live session and a script are interchangeable.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "core/script.h"
#include "core/simulator.h"

namespace gui {

struct RadioPanelState {
  // IR
  int irProtocol = 0;
  unsigned irAddress = 0x04, irCommand = 0x08;
  // NFC
  std::string nfcUid = "04A2245A6B1C80";
  int nfcType = 0;
  int nfcBlocks = 4;
  int nfcDump = 0;
  // BLE
  std::string bleHost = "MacBook";
  bool bondsFull = false;
};

struct ScriptPanelState {
  sim::ScriptPlayer player;
  int picked = 0;
  bool restartFirst = true;
  std::string message;
};

void drawIrPanel(sim::Simulator& s, RadioPanelState& st);
void drawNfcPanel(sim::Simulator& s, RadioPanelState& st);
void drawWifiPanel(sim::Simulator& s);
void drawBlePanel(sim::Simulator& s, RadioPanelState& st);
void drawScriptPanel(sim::Simulator& s, ScriptPanelState& st, const std::filesystem::path& project);

}  // namespace gui

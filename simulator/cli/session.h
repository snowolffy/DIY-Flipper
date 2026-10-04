// session.h - where a running session is found: <project>/.sim-session.json holds its port, access token
// and process id. Every terminal command reads it; the window gets the token in its URL.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace simcli {

struct SessionInfo {
  int port = 0;
  std::string token;
  long pid = 0;
  std::string project;
  bool window = false;  // started by `sim gui` (ends when its window closes)
};

std::filesystem::path sessionFile(const std::filesystem::path& project);
bool readSession(const std::filesystem::path& project, SessionInfo& out);
bool writeSession(const std::filesystem::path& project, const SessionInfo& s);
void removeSession(const std::filesystem::path& project);

// --project DIR, else ./sim when it holds scripts/ or storage/, else the current folder.
std::filesystem::path resolveProject(const std::string& given);
std::string randomToken();

// Starts `<this exe> serve ...` in the background, detached from the terminal.
bool spawnServer(const std::vector<std::string>& args, std::string& err);
std::string selfExe();
// Chrome / Chromium / Edge for --app windows and headless screenshots; empty if none.
std::string findBrowser();
bool openWindow(const std::string& url);
bool openFolder(const std::filesystem::path& p);

}  // namespace simcli

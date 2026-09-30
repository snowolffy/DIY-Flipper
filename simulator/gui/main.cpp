// sim_gui - the windowed simulator: the firmware running on real time, its screen scaled up, keyboard and
// on-screen buttons for input, and live controls for the mocks. Storage is the project's own
// sim/storage folder, so what the firmware writes stays on disk between runs.
//
//   sim_gui [--project DIR] [--scale N] [--script FILE.json] [--tab NAME] [--screenshot FILE.bmp --frames N]
//
//   --script   play a mock-script as soon as the window opens (same as Scripts > Run here)
//   --tab      open the Mock control panel on this tab: project, power, ir, nfc, wifi, bluetooth, scripts
//   --import   import a Flipper UI Studio export (.zip / .b1i / .b1f) into the project's SD card at start
//
// Drop a folder on the window to open it as the project; drop an export to import it.
//
// Keys: Left/Up = LEFT, Right/Down = RIGHT, Enter/Z = OK, Backspace/Esc/X = CANCEL, P = POWER,
//       F12 = save the device screen as a BMP in <project>/screenshots.
#include <SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "core/importer.h"
#include "core/simulator.h"
#include "gui/panels.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "misc/cpp/imgui_stdlib.h"

namespace fs = std::filesystem;

namespace {

struct Options {
  fs::path project = "sim";
  bool projectGiven = false;
  int scale = 3;
  fs::path screenshot;  // write the whole window to this BMP after `frames` frames, then quit
  int frames = 0;
  fs::path script;
  std::string tab;
  fs::path import;
};

// Paper and ink of the panel as the studio draws them, so screens look the same in both tools.
constexpr uint32_t kLit = 0xFFF5F5F5;
constexpr uint32_t kDark = 0xFF0B1013;

const char* kSdFolders[] = {"ir", "nfc", "games", "media", "system"};

bool parseArgs(int argc, char** argv, Options& o) {
  for (int i = 1; i < argc; i++) {
    const std::string a = argv[i];
    if (a == "--project" && i + 1 < argc) {
      o.project = argv[++i];
      o.projectGiven = true;
    }
    else if (a == "--scale" && i + 1 < argc) o.scale = std::max(1, std::min(8, std::atoi(argv[++i])));
    else if (a == "--screenshot" && i + 1 < argc) o.screenshot = argv[++i];
    else if (a == "--frames" && i + 1 < argc) o.frames = std::atoi(argv[++i]);
    else if (a == "--script" && i + 1 < argc) o.script = argv[++i];
    else if (a == "--tab" && i + 1 < argc) o.tab = argv[++i];
    else if (a == "--import" && i + 1 < argc) o.import = fs::u8path(argv[++i]);
    else return false;
  }
  return true;
}

void ensureProjectFolders(const fs::path& project) {
  std::error_code ec;
  fs::create_directories(project / "storage" / "flash", ec);
  for (const char* d : kSdFolders) fs::create_directories(project / "storage" / "sd" / d, ec);
}

hal::DateTime pcLocalTime() {
  const std::time_t t = std::time(nullptr);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  hal::DateTime d;
  d.year = (uint16_t)(tm.tm_year + 1900);
  d.month = (uint8_t)(tm.tm_mon + 1);
  d.day = (uint8_t)tm.tm_mday;
  d.hour = (uint8_t)tm.tm_hour;
  d.minute = (uint8_t)tm.tm_min;
  d.second = (uint8_t)tm.tm_sec;
  return d;
}

void fillPixels(const sim::MockDisplay& disp, uint32_t* px) {
  for (int16_t y = 0; y < ui::kScreenH; y++)
    for (int16_t x = 0; x < ui::kScreenW; x++) px[y * ui::kScreenW + x] = disp.lit(x, y) ? kLit : kDark;
}

// ---- recent project folders, one per line in SDL's per-user preferences folder ----
fs::path recentFile() {
  char* pref = SDL_GetPrefPath("DIY-Flipper", "Simulator");
  if (!pref) return {};
  const fs::path p = fs::u8path(pref) / "recent.txt";
  SDL_free(pref);
  return p;
}

std::vector<std::string> loadRecent() {
  std::vector<std::string> out;
  std::ifstream f(recentFile());
  std::string line;
  while (std::getline(f, line))
    if (!line.empty()) out.push_back(line);
  return out;
}

void saveRecent(const std::vector<std::string>& recent) {
  const fs::path p = recentFile();
  if (p.empty()) return;
  std::ofstream f(p, std::ios::trunc);
  for (const auto& r : recent) f << r << "\n";
}

void rememberProject(std::vector<std::string>& recent, const fs::path& project) {
  std::error_code ec;
  const std::string abs = fs::absolute(project, ec).lexically_normal().u8string();
  recent.erase(std::remove(recent.begin(), recent.end(), abs), recent.end());
  recent.insert(recent.begin(), abs);
  if (recent.size() > 8) recent.resize(8);
  saveRecent(recent);
}

// Saves the device screen at 4x as a BMP and returns its path (empty on failure).
std::string saveScreen(const sim::MockDisplay& disp, const fs::path& project) {
  constexpr int k = 4;
  SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, ui::kScreenW * k, ui::kScreenH * k, 32, SDL_PIXELFORMAT_ARGB8888);
  if (!s) return {};
  auto* px = static_cast<uint32_t*>(s->pixels);
  const int pitch = s->pitch / 4;
  for (int y = 0; y < ui::kScreenH * k; y++)
    for (int x = 0; x < ui::kScreenW * k; x++) px[y * pitch + x] = disp.lit((int16_t)(x / k), (int16_t)(y / k)) ? kLit : kDark;
  std::error_code ec;
  fs::create_directories(project / "screenshots", ec);
  char name[64];
  const std::time_t t = std::time(nullptr);
  std::strftime(name, sizeof(name), "screen-%Y%m%d-%H%M%S.bmp", std::localtime(&t));
  const fs::path out = project / "screenshots" / name;
  const bool ok = SDL_SaveBMP(s, out.string().c_str()) == 0;
  SDL_FreeSurface(s);
  return ok ? out.string() : std::string();
}

bool saveWindow(SDL_Renderer* r, int w, int h, const fs::path& out) {
  SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
  if (!s) return false;
  bool ok = SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) == 0 &&
            SDL_SaveBMP(s, out.string().c_str()) == 0;
  SDL_FreeSurface(s);
  return ok;
}

// A button that reports true for every frame it is held down, so it acts like a real key.
bool holdButton(const char* label, const ImVec2& size) {
  ImGui::Button(label, size);
  return ImGui::IsItemActive();
}

}  // namespace

int main(int argc, char** argv) {
  Options opt;
  if (!parseArgs(argc, argv, opt)) {
    std::fprintf(stderr,
                 "usage: sim_gui [--project DIR] [--scale N] [--script FILE.json] [--tab NAME] [--import FILE] "
                 "[--screenshot FILE.bmp --frames N]\n");
    return 2;
  }
  std::vector<std::string> recent = loadRecent();
  // no --project: reopen the last project if it still exists, else ./sim
  if (!opt.projectGiven && !recent.empty() && fs::is_directory(fs::u8path(recent.front()))) opt.project = fs::u8path(recent.front());
  ensureProjectFolders(opt.project);

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "DIY Flipper Simulator", SDL_GetError(), nullptr);
    return 1;
  }
  SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
  const int winW = 520 + ui::kScreenW * opt.scale;
  const int winH = std::max(600, ui::kScreenH * opt.scale + 150);
  SDL_Window* window = SDL_CreateWindow("DIY Flipper Simulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, winW,
                                        winH, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  if (!window) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "DIY Flipper Simulator", SDL_GetError(), nullptr);
    return 1;
  }
  SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
  if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
  if (!renderer) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "DIY Flipper Simulator", SDL_GetError(), window);
    return 1;
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;  // fixed layout; nothing to remember between runs
  ImGui::StyleColorsDark();
  ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
  ImGui_ImplSDLRenderer2_Init(renderer);

  SDL_Texture* screenTex =
      SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, ui::kScreenW, ui::kScreenH);
  SDL_SetTextureScaleMode(screenTex, SDL_ScaleModeNearest);
  static uint32_t pixels[ui::kScreenW * ui::kScreenH];

  int batteryPct = 80;
  bool batteryUnknown = false;
  bool rtcMissing = false;
  bool sdPresent = true;
  bool failWrites = false;
  std::unique_ptr<sim::Simulator> simPtr;
  fs::path project;
  // A fresh device for a project folder: storage is that folder's storage/, mocks start from defaults.
  auto openProject = [&](const fs::path& dir, bool coldBoot) {
    ensureProjectFolders(dir);
    project = dir;
    simPtr = std::make_unique<sim::Simulator>(dir / "storage");
    simPtr->rtc().set(pcLocalTime());
    batteryPct = 80;
    batteryUnknown = rtcMissing = failWrites = false;
    sdPresent = true;
    simPtr->battery().setPercent(batteryPct);
    simPtr->boot(coldBoot);
    rememberProject(recent, dir);
    SDL_SetWindowTitle(window, ("DIY Flipper Simulator - " + fs::absolute(dir).filename().u8string()).c_str());
  };

  sim::Script startScript;
  std::string startError;
  const bool haveStartScript = !opt.script.empty() && sim::loadScript(opt.script, startScript, startError) &&
                               sim::copySeed(opt.project, startScript.init.storageSeed, opt.project / "storage", startError);
  openProject(opt.project, haveStartScript ? startScript.init.coldBoot : true);
  if (haveStartScript) sim::applyInitialState(startScript.init, *simPtr);

  std::string lastShot;
  std::string openPath, importPath;
  sim::ImportResult lastImport;
  std::string lastImportFile;
  fs::path pendingOpen, pendingImport;
  gui::RadioPanelState radios;
  gui::ScriptPanelState scripts;
  bool uiDown[hal::kButtonCount] = {};  // last button state written by keyboard/mouse
  int frame = 0;
  if (!opt.script.empty()) {
    if (haveStartScript) scripts.player.start(std::move(startScript), simPtr->now());
    else scripts.message = startError;
    if (opt.tab.empty()) opt.tab = "scripts";
  }
  std::string pendingTab = opt.tab;
  auto tabFlags = [&](const char* id) {
    // select a requested tab once, then leave it to the user
    return pendingTab == id ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
  };
  if (!opt.import.empty()) {
    pendingImport = opt.import;
  }

  uint64_t start = SDL_GetTicks64();
  int frameStart = 0;
  bool running = true;
  while (running) {
    SDL_Event ev;
    bool saveShot = false;
    while (SDL_PollEvent(&ev)) {
      ImGui_ImplSDL2_ProcessEvent(&ev);
      if (ev.type == SDL_QUIT) running = false;
      if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_CLOSE &&
          ev.window.windowID == SDL_GetWindowID(window))
        running = false;
      if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_F12 && !ev.key.repeat) saveShot = true;
      if (ev.type == SDL_DROPFILE) {
        // a folder opens as a project; a file is imported into this project's SD card
        const fs::path dropped = fs::u8path(ev.drop.file);
        SDL_free(ev.drop.file);
        if (fs::is_directory(dropped)) pendingOpen = dropped;
        else pendingImport = dropped;
      }
    }
    if (!pendingOpen.empty()) {
      scripts.player.stop();
      openProject(pendingOpen, true);
      pendingOpen.clear();
      start = SDL_GetTicks64();
      frameStart = frame;
      pendingTab = "project";
    }
    if (!pendingImport.empty()) {
      lastImport = sim::importAsset(pendingImport, simPtr->storage());
      lastImportFile = pendingImport.filename().u8string();
      pendingImport.clear();
      pendingTab = "project";
    }
    sim::Simulator& s = *simPtr;

    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    int w, h;
    SDL_GetRendererOutputSize(renderer, &w, &h);
    const float devW = (float)(ui::kScreenW * opt.scale) + 32.0f;

    // ---- device ----
    bool mouseDown[hal::kButtonCount] = {};
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(devW, (float)h));
    ImGui::Begin("Device", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
    fillPixels(s.display(), pixels);
    SDL_UpdateTexture(screenTex, nullptr, pixels, ui::kScreenW * 4);
    // grey edge so the dark panel reads as a screen against the dark window
    ImGui::Image((ImTextureID)(intptr_t)screenTex,
                 ImVec2((float)(ui::kScreenW * opt.scale), (float)(ui::kScreenH * opt.scale)), ImVec2(0, 0),
                 ImVec2(1, 1), ImVec4(1, 1, 1, 1), ImVec4(0.45f, 0.48f, 0.52f, 1.0f));
    const float bw = ((float)(ui::kScreenW * opt.scale) - 2 * ImGui::GetStyle().ItemSpacing.x) / 3.0f;
    mouseDown[(int)hal::Button::Left] = holdButton("<  Left", ImVec2(bw, 36));
    ImGui::SameLine();
    mouseDown[(int)hal::Button::Ok] = holdButton("OK", ImVec2(bw, 36));
    ImGui::SameLine();
    mouseDown[(int)hal::Button::Right] = holdButton("Right  >", ImVec2(bw, 36));
    mouseDown[(int)hal::Button::Cancel] = holdButton("Back", ImVec2(bw * 1.5f + ImGui::GetStyle().ItemSpacing.x / 2, 30));
    ImGui::SameLine();
    mouseDown[(int)hal::Button::Power] = holdButton("Power", ImVec2(bw * 1.5f + ImGui::GetStyle().ItemSpacing.x / 2, 30));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("Keys: arrows = Left/Right, Enter or Z = OK, Esc, Backspace or X = Back, P = Power, "
                       "F12 = save screen");
    ImGui::PopStyleColor();
    ImGui::End();

    // ---- mock control ----
    ImGui::SetNextWindowPos(ImVec2(devW, 0));
    ImGui::SetNextWindowSize(ImVec2((float)w - devW, (float)h));
    ImGui::Begin("Mock control", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    ImGui::Text("Menu: %s", s.app().menuPath().c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("  uptime %.1f s", s.now() / 1000.0);
    const bool tabs = ImGui::BeginTabBar("mocks");
    if (tabs && ImGui::BeginTabItem("Project", nullptr, tabFlags("project"))) {
      std::error_code ec;
      ImGui::TextWrapped("Folder: %s", fs::absolute(project, ec).u8string().c_str());
      ImGui::SetNextItemWidth(260);
      ImGui::InputTextWithHint("##open", "path to a project folder", &openPath);
      ImGui::SameLine();
      if (ImGui::Button("Open") && !openPath.empty()) {
        if (fs::is_directory(fs::u8path(openPath))) pendingOpen = fs::u8path(openPath);
        else lastImport = {false, "no such folder: " + openPath, {}, {}, {}};
      }
      ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
      ImGui::TextWrapped("Or drop a folder onto the window. Missing storage folders are created.");
      ImGui::PopStyleColor();
      if (ImGui::CollapsingHeader("Recent", ImGuiTreeNodeFlags_DefaultOpen)) {
        int remove = -1;
        for (int i = 0; i < (int)recent.size(); i++) {
          ImGui::PushID(i);
          const bool current = fs::equivalent(fs::u8path(recent[i]), project, ec);
          ImGui::BeginDisabled(current || !fs::is_directory(fs::u8path(recent[i]), ec));
          if (ImGui::Selectable(recent[i].c_str(), current, 0, ImVec2(ImGui::GetContentRegionAvail().x - 30, 0)))
            pendingOpen = fs::u8path(recent[i]);
          ImGui::EndDisabled();
          if (!current) {
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) remove = i;
          }
          ImGui::PopID();
        }
        if (remove >= 0) {
          recent.erase(recent.begin() + remove);
          saveRecent(recent);
        }
      }
      if (ImGui::CollapsingHeader("Import asset", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextWrapped("Exports from Flipper UI Studio go onto this project's SD card: theme .zip to "
                           "/system/theme/<name>, .b1i to /media, .b1f to /system/fonts. Drop a file on the window, "
                           "or give its path.");
        ImGui::SetNextItemWidth(260);
        ImGui::InputTextWithHint("##import", "path to a .zip, .b1i or .b1f", &importPath);
        ImGui::SameLine();
        if (ImGui::Button("Import") && !importPath.empty()) pendingImport = fs::u8path(importPath);
        if (!lastImportFile.empty() || !lastImport.error.empty()) {
          ImGui::Separator();
          if (lastImport.ok) {
            ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.55f, 1), "Imported %s", lastImportFile.c_str());
            for (const auto& p : lastImport.written) ImGui::BulletText("%s", p.c_str());
            if (!lastImport.themes.empty())
              ImGui::TextWrapped("Pick it on the device: Settings > Theme.");
          } else {
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.4f, 1), "Not imported: %s", lastImport.error.c_str());
          }
          for (const auto& wn : lastImport.warnings) ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1), "! %s", wn.c_str());
        }
      }
      ImGui::EndTabItem();
    }
    if (tabs && ImGui::BeginTabItem("Power & storage", nullptr, tabFlags("power"))) {
    if (ImGui::CollapsingHeader("Battery", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::BeginDisabled(batteryUnknown);
      if (ImGui::SliderInt("Charge %", &batteryPct, 0, 100)) s.battery().setPercent(batteryPct);
      ImGui::EndDisabled();
      if (ImGui::Checkbox("No reading (ADC returns 0)", &batteryUnknown))
        s.battery().setPercent(batteryUnknown ? -1 : batteryPct);
      const int shown = s.app().battery().percent();
      ImGui::TextDisabled("Firmware sees %s (averaged over 8 s)",
                          shown < 0 ? "no reading" : (std::to_string(shown) + "%").c_str());
    }

    if (ImGui::CollapsingHeader("Clock (DS3231)", ImGuiTreeNodeFlags_DefaultOpen)) {
      if (ImGui::Checkbox("RTC missing", &rtcMissing)) s.rtc().setMissing(rtcMissing);
      ImGui::SameLine();
      if (ImGui::Button("Set to PC time")) {
        s.rtc().set(pcLocalTime());
        rtcMissing = false;
      }
      hal::DateTime t;
      if (s.rtc().now(t))
        ImGui::Text("%04u-%02u-%02u %02u:%02u:%02u", t.year, t.month, t.day, t.hour, t.minute, t.second);
      else
        ImGui::TextDisabled("no time");
    }

    if (ImGui::CollapsingHeader("Storage", ImGuiTreeNodeFlags_DefaultOpen)) {
      if (ImGui::Checkbox("SD card inserted", &sdPresent)) s.storage().setSdPresent(sdPresent);
      if (ImGui::Checkbox("Fail every write", &failWrites)) s.storage().setFailWrites(failWrites);
      std::error_code ec;
      ImGui::TextWrapped("Folder: %s", fs::absolute(project / "storage", ec).u8string().c_str());
    }

    if (ImGui::CollapsingHeader("Firmware state", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::Text("Invert:  %s", s.app().settings().invert ? "on" : "off");
      ImGui::Text("fb_hash: %s", s.display().hash().c_str());
      if (ImGui::Button("Save screen (F12)")) saveShot = true;
      if (!lastShot.empty()) ImGui::TextWrapped("Saved %s", lastShot.c_str());
    }
      ImGui::EndTabItem();
    }
    if (tabs && ImGui::BeginTabItem("IR", nullptr, tabFlags("ir"))) {
      gui::drawIrPanel(s, radios);
      ImGui::EndTabItem();
    }
    if (tabs && ImGui::BeginTabItem("NFC", nullptr, tabFlags("nfc"))) {
      gui::drawNfcPanel(s, radios);
      ImGui::EndTabItem();
    }
    if (tabs && ImGui::BeginTabItem("WiFi", nullptr, tabFlags("wifi"))) {
      gui::drawWifiPanel(s);
      ImGui::EndTabItem();
    }
    if (tabs && ImGui::BeginTabItem("Bluetooth", nullptr, tabFlags("bluetooth"))) {
      gui::drawBlePanel(s, radios);
      ImGui::EndTabItem();
    }
    if (tabs && ImGui::BeginTabItem("Scripts", nullptr, tabFlags("scripts"))) {
      gui::drawScriptPanel(s, scripts, project);
      ImGui::EndTabItem();
    }
    if (tabs) ImGui::EndTabBar();
    pendingTab.clear();
    ImGui::End();

    if (saveShot) {
      lastShot = saveScreen(s.display(), project);
      if (lastShot.empty()) lastShot = "(couldn't save)";
    }

    // ---- input: keyboard OR on-screen button ----
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    const bool kb = !io.WantCaptureKeyboard;
    auto key = [&](SDL_Scancode a, SDL_Scancode b = SDL_SCANCODE_UNKNOWN, SDL_Scancode c = SDL_SCANCODE_UNKNOWN) {
      return kb && (keys[a] || (b != SDL_SCANCODE_UNKNOWN && keys[b]) || (c != SDL_SCANCODE_UNKNOWN && keys[c]));
    };
    bool want[hal::kButtonCount];
    want[(int)hal::Button::Left] = mouseDown[(int)hal::Button::Left] || key(SDL_SCANCODE_LEFT, SDL_SCANCODE_UP);
    want[(int)hal::Button::Right] = mouseDown[(int)hal::Button::Right] || key(SDL_SCANCODE_RIGHT, SDL_SCANCODE_DOWN);
    want[(int)hal::Button::Ok] =
        mouseDown[(int)hal::Button::Ok] || key(SDL_SCANCODE_RETURN, SDL_SCANCODE_Z, SDL_SCANCODE_KP_ENTER);
    want[(int)hal::Button::Cancel] =
        mouseDown[(int)hal::Button::Cancel] || key(SDL_SCANCODE_ESCAPE, SDL_SCANCODE_BACKSPACE, SDL_SCANCODE_X);
    want[(int)hal::Button::Power] = mouseDown[(int)hal::Button::Power] || key(SDL_SCANCODE_P);
    // only on change, so a running script's button presses aren't overwritten every frame
    for (int b = 0; b < hal::kButtonCount; b++) {
      if (want[b] != uiDown[b]) s.input().set((hal::Button)b, want[b]);
      uiDown[b] = want[b];
    }

    // ---- run the firmware up to real time (screenshot mode steps a fixed 1/60 s so output is repeatable) ----
    const uint64_t elapsed =
        opt.screenshot.empty() ? SDL_GetTicks64() - start : (uint64_t)(frame - frameStart) * 1000 / 60;
    scripts.player.runUntil(s, (uint32_t)elapsed);
    s.advanceTo((uint32_t)elapsed);

    ImGui::Render();
    SDL_SetRenderDrawColor(renderer, 24, 26, 30, 255);
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);

    frame++;
    if (!opt.screenshot.empty() && frame >= opt.frames) {
      const bool ok = saveWindow(renderer, w, h, opt.screenshot);
      std::printf("%s %s\n", ok ? "saved" : "couldn't save", opt.screenshot.string().c_str());
      running = false;
    }
    SDL_RenderPresent(renderer);
  }

  ImGui_ImplSDLRenderer2_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  SDL_DestroyTexture(screenTex);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}

// sim_gui - the windowed simulator: the firmware running on real time, its screen scaled up, keyboard and
// on-screen buttons for input, and live controls for the mocks. Storage is the project's own
// sim/storage folder, so what the firmware writes stays on disk between runs.
//
//   sim_gui [--project DIR] [--scale N] [--screenshot FILE.bmp --frames N]
//
// Keys: Left/Up = LEFT, Right/Down = RIGHT, Enter/Z = OK, Backspace/Esc/X = CANCEL, P = POWER,
//       F12 = save the device screen as a BMP in <project>/screenshots.
#include <SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <string>

#include "core/simulator.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

namespace fs = std::filesystem;

namespace {

struct Options {
  fs::path project = "sim";
  int scale = 3;
  fs::path screenshot;  // write the whole window to this BMP after `frames` frames, then quit
  int frames = 0;
};

// Paper and ink of the panel as the studio draws them, so screens look the same in both tools.
constexpr uint32_t kLit = 0xFFF5F5F5;
constexpr uint32_t kDark = 0xFF0B1013;

const char* kSdFolders[] = {"ir", "nfc", "games", "media", "system"};

bool parseArgs(int argc, char** argv, Options& o) {
  for (int i = 1; i < argc; i++) {
    const std::string a = argv[i];
    if (a == "--project" && i + 1 < argc) o.project = argv[++i];
    else if (a == "--scale" && i + 1 < argc) o.scale = std::max(1, std::min(8, std::atoi(argv[++i])));
    else if (a == "--screenshot" && i + 1 < argc) o.screenshot = argv[++i];
    else if (a == "--frames" && i + 1 < argc) o.frames = std::atoi(argv[++i]);
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
    std::fprintf(stderr, "usage: sim_gui [--project DIR] [--scale N] [--screenshot FILE.bmp --frames N]\n");
    return 2;
  }
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

  sim::Simulator s(opt.project / "storage");
  s.rtc().set(pcLocalTime());
  s.battery().setPercent(80);
  s.boot(true);

  int batteryPct = 80;
  bool batteryUnknown = false;
  bool rtcMissing = false;
  bool sdPresent = true;
  bool failWrites = false;
  std::string lastShot;

  const uint64_t start = SDL_GetTicks64();
  int frame = 0;
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
    }

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
      ImGui::TextWrapped("Folder: %s", fs::absolute(opt.project / "storage", ec).string().c_str());
    }

    if (ImGui::CollapsingHeader("Firmware state", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::Text("Menu:    %s", s.app().menuPath().c_str());
      ImGui::Text("Invert:  %s", s.app().settings().invert ? "on" : "off");
      ImGui::Text("Uptime:  %.1f s", s.now() / 1000.0);
      ImGui::Text("fb_hash: %s", s.display().hash().c_str());
      if (ImGui::Button("Save screen (F12)")) saveShot = true;
      if (!lastShot.empty()) ImGui::TextWrapped("Saved %s", lastShot.c_str());
    }
    ImGui::End();

    if (saveShot) {
      lastShot = saveScreen(s.display(), opt.project);
      if (lastShot.empty()) lastShot = "(couldn't save)";
    }

    // ---- input: keyboard OR on-screen button ----
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    const bool kb = !io.WantCaptureKeyboard;
    auto key = [&](SDL_Scancode a, SDL_Scancode b = SDL_SCANCODE_UNKNOWN, SDL_Scancode c = SDL_SCANCODE_UNKNOWN) {
      return kb && (keys[a] || (b != SDL_SCANCODE_UNKNOWN && keys[b]) || (c != SDL_SCANCODE_UNKNOWN && keys[c]));
    };
    s.input().set(hal::Button::Left, mouseDown[(int)hal::Button::Left] || key(SDL_SCANCODE_LEFT, SDL_SCANCODE_UP));
    s.input().set(hal::Button::Right, mouseDown[(int)hal::Button::Right] || key(SDL_SCANCODE_RIGHT, SDL_SCANCODE_DOWN));
    s.input().set(hal::Button::Ok, mouseDown[(int)hal::Button::Ok] || key(SDL_SCANCODE_RETURN, SDL_SCANCODE_Z, SDL_SCANCODE_KP_ENTER));
    s.input().set(hal::Button::Cancel,
                  mouseDown[(int)hal::Button::Cancel] || key(SDL_SCANCODE_ESCAPE, SDL_SCANCODE_BACKSPACE, SDL_SCANCODE_X));
    s.input().set(hal::Button::Power, mouseDown[(int)hal::Button::Power] || key(SDL_SCANCODE_P));

    // ---- run the firmware up to real time (screenshot mode steps a fixed 1/60 s so output is repeatable) ----
    const uint64_t elapsed = opt.screenshot.empty() ? SDL_GetTicks64() - start : (uint64_t)frame * 1000 / 60;
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

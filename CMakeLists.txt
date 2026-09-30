cmake_minimum_required(VERSION 3.16)
project(diy_flipper LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# static MSVC runtime, so the Windows build runs without installing a redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(DIYF_BUILD_GUI "Build sim_gui, the windowed simulator (fetches SDL2 and Dear ImGui at configure time)" OFF)

# warnings for our own targets only, not for fetched third-party code
add_library(diyf_warnings INTERFACE)
if(MSVC)
  target_compile_options(diyf_warnings INTERFACE /W4 /permissive- /utf-8)
else()
  target_compile_options(diyf_warnings INTERFACE -Wall -Wextra)
endif()

# ---- firmware core: the code that also runs on the ESP32. No GUI, OS or Arduino headers. ----
add_library(firmware_core STATIC
  firmware/ui/framebuffer.cpp
  firmware/ui/gfx.cpp
  firmware/app/app.cpp
  firmware/app/battery.cpp
  firmware/app/buttons.cpp
  firmware/app/minijson.cpp
  firmware/app/radio_apps.cpp
  firmware/app/screens.cpp
  firmware/app/settings.cpp
  firmware/app/theme.cpp
  firmware/assets/assets.cpp
)
target_include_directories(firmware_core PUBLIC firmware)
# stand-in <Arduino.h> for the exported asset headers; the ESP32 build uses the real one
target_include_directories(firmware_core PRIVATE firmware/platform/host)
target_link_libraries(firmware_core PRIVATE diyf_warnings)

# ---- zip reading for Import Asset (vendored miniz 3.0.2, MIT) ----
add_library(miniz STATIC simulator/third_party/miniz/miniz.c)
target_include_directories(miniz PUBLIC simulator/third_party)

# ---- simulator core: mocks, virtual clock, script runner, asset importer ----
add_library(sim_core STATIC
  simulator/core/importer.cpp
  simulator/core/mocks.cpp
  simulator/core/simulator.cpp
  simulator/core/script.cpp
)
target_include_directories(sim_core PUBLIC simulator)
target_include_directories(sim_core SYSTEM PRIVATE simulator/third_party)
target_link_libraries(sim_core PUBLIC firmware_core PRIVATE miniz diyf_warnings)

# ---- headless runner (cloud / CI) ----
add_executable(sim_headless simulator/headless/main.cpp)
target_link_libraries(sim_headless PRIVATE sim_core diyf_warnings)

# ---- unit tests for firmware logic that scripts can't reach precisely ----
add_executable(unit_tests tests/unit_tests.cpp)
target_link_libraries(unit_tests PRIVATE sim_core miniz diyf_warnings)

enable_testing()
add_test(NAME unit_tests COMMAND unit_tests)
file(GLOB DIYF_SCRIPTS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/sim/scripts/*.json)
foreach(script ${DIYF_SCRIPTS})
  get_filename_component(script_name ${script} NAME_WE)
  add_test(NAME script_${script_name} COMMAND sim_headless ${script})
endforeach()

# ---- windowed simulator ----
if(DIYF_BUILD_GUI)
  include(FetchContent)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST OFF CACHE BOOL "" FORCE)
  set(SDL_FORCE_STATIC_VCRT ON CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-2.30.9
    GIT_SHALLOW TRUE)
  FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.5
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(SDL2 imgui)

  add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer2.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
  )
  target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
  target_link_libraries(imgui PUBLIC SDL2::SDL2-static)

  add_executable(sim_gui WIN32 simulator/gui/main.cpp simulator/gui/panels.cpp)
  target_link_libraries(sim_gui PRIVATE sim_core imgui diyf_warnings)
  if(TARGET SDL2::SDL2main)
    target_link_libraries(sim_gui PRIVATE SDL2::SDL2main)
  endif()

  # starts the window on SDL's offscreen driver and saves one frame, so CI proves the GUI still runs
  if(UNIX AND NOT APPLE)
    add_test(NAME gui_smoke COMMAND sim_gui --project ${CMAKE_BINARY_DIR}/gui_smoke_project
             --screenshot ${CMAKE_BINARY_DIR}/gui_smoke.bmp --frames 120)
    set_tests_properties(gui_smoke PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=offscreen")
  endif()
endif()

cmake_minimum_required(VERSION 3.16)
project(diy_flipper LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
else()
  add_compile_options(-Wall -Wextra)
endif()

# ---- firmware core: the code that also runs on the ESP32. No GUI, OS or Arduino headers. ----
add_library(firmware_core STATIC
  firmware/ui/framebuffer.cpp
  firmware/ui/gfx.cpp
  firmware/app/app.cpp
  firmware/app/battery.cpp
  firmware/app/buttons.cpp
  firmware/app/screens.cpp
  firmware/app/settings.cpp
  firmware/assets/assets.cpp
)
target_include_directories(firmware_core PUBLIC firmware)
# stand-in <Arduino.h> for the exported asset headers; the ESP32 build uses the real one
target_include_directories(firmware_core PRIVATE firmware/platform/host)

# ---- simulator core: mocks, virtual clock, script runner ----
add_library(sim_core STATIC
  simulator/core/mocks.cpp
  simulator/core/simulator.cpp
  simulator/core/script.cpp
)
target_include_directories(sim_core PUBLIC simulator)
target_include_directories(sim_core SYSTEM PRIVATE simulator/third_party)
target_link_libraries(sim_core PUBLIC firmware_core)

# ---- headless runner (cloud / CI) ----
add_executable(sim_headless simulator/headless/main.cpp)
target_link_libraries(sim_headless PRIVATE sim_core)

# ---- unit tests for firmware logic that scripts can't reach precisely ----
add_executable(unit_tests tests/unit_tests.cpp)
target_link_libraries(unit_tests PRIVATE sim_core)

enable_testing()
add_test(NAME unit_tests COMMAND unit_tests)
file(GLOB DIYF_SCRIPTS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/sim/scripts/*.json)
foreach(script ${DIYF_SCRIPTS})
  get_filename_component(script_name ${script} NAME_WE)
  add_test(NAME script_${script_name} COMMAND sim_headless ${script})
endforeach()

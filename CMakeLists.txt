cmake_minimum_required(VERSION 3.21)
project(diy_flipper LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# static MSVC runtime, so the Windows build runs without installing a redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")


# warnings for our own targets only, not for fetched third-party code
add_library(diyf_warnings INTERFACE)
if(MSVC)
  target_compile_options(diyf_warnings INTERFACE /W4 /permissive- /utf-8)
else()
  target_compile_options(diyf_warnings INTERFACE -Wall -Wextra)
endif()

# ---- firmware core: the code that also runs on the ESP32-S3. No GUI, OS or Arduino headers. ----
file(GLOB DIYF_FIRMWARE_SOURCES CONFIGURE_DEPENDS
  firmware/ui/*.cpp firmware/app/*.cpp firmware/app/modules/*.cpp firmware/assets/*.cpp)
add_library(firmware_core STATIC ${DIYF_FIRMWARE_SOURCES})
target_include_directories(firmware_core PUBLIC firmware)
# stand-in <Arduino.h> for the exported asset headers; the ESP32 build uses the real one
target_include_directories(firmware_core PRIVATE firmware/platform/host)
target_link_libraries(firmware_core PRIVATE diyf_warnings)

# ---- PNG writing and zip reading (vendored miniz 3.0.2, MIT) ----
add_library(miniz STATIC simulator/third_party/miniz/miniz.c)
target_include_directories(miniz PUBLIC simulator/third_party)

# ---- emulator core: mocks, virtual clock, commands, script runner, asset importer ----
add_library(sim_core STATIC
  simulator/core/commands.cpp
  simulator/core/importer.cpp
  simulator/core/mocks.cpp
  simulator/core/simulator.cpp
  simulator/core/script.cpp
)
target_include_directories(sim_core PUBLIC simulator)
target_include_directories(sim_core SYSTEM PUBLIC simulator/third_party)
target_link_libraries(sim_core PUBLIC firmware_core PRIVATE miniz diyf_warnings)

# ---- sim: the one emulator exe (window server, terminal commands, script runner) ----
file(GLOB DIYF_SIM_SOURCES CONFIGURE_DEPENDS simulator/cli/*.cpp)
file(GLOB_RECURSE DIYF_WEB_FILES CONFIGURE_DEPENDS simulator/web/*)
add_custom_command(
  OUTPUT ${CMAKE_BINARY_DIR}/web_assets.cpp
  COMMAND ${CMAKE_COMMAND} -DDIR=${CMAKE_SOURCE_DIR}/simulator/web -DOUT=${CMAKE_BINARY_DIR}/web_assets.cpp
          -P ${CMAKE_SOURCE_DIR}/cmake/embed_web.cmake
  DEPENDS ${DIYF_WEB_FILES} ${CMAKE_SOURCE_DIR}/cmake/embed_web.cmake
  COMMENT "Embedding the emulator window files")
add_executable(sim ${DIYF_SIM_SOURCES} ${CMAKE_BINARY_DIR}/web_assets.cpp)
target_link_libraries(sim PRIVATE sim_core miniz diyf_warnings)
find_package(Threads REQUIRED)
target_link_libraries(sim PRIVATE Threads::Threads)
if(WIN32)
  target_link_libraries(sim PRIVATE ws2_32)
endif()

# ---- unit tests for firmware logic that scripts can't reach precisely ----
add_executable(unit_tests tests/unit_tests.cpp)
target_link_libraries(unit_tests PRIVATE sim_core miniz diyf_warnings)

enable_testing()
add_test(NAME unit_tests COMMAND unit_tests ${CMAKE_SOURCE_DIR})
file(GLOB DIYF_SCRIPTS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/sim/scripts/*.json)
foreach(script ${DIYF_SCRIPTS})
  get_filename_component(script_name ${script} NAME_WE)
  add_test(NAME script_${script_name} COMMAND sim run ${script})
endforeach()

# ---- app rules: a level-1 app without onPauseRequest() / a level-2 app without onRawInput() must not build ----
foreach(rule CONTROL LEVEL1_WITHOUT_PAUSE LEVEL2_WITHOUT_RAW)
  string(TOLOWER ${rule} rule_lower)
  add_executable(rules_${rule_lower} EXCLUDE_FROM_ALL tests/app_rules/rules_check.cpp)
  target_compile_definitions(rules_${rule_lower} PRIVATE RULES_${rule})
  target_link_libraries(rules_${rule_lower} PRIVATE firmware_core)
  add_test(NAME app_rules_${rule_lower}
           COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target rules_${rule_lower} --config $<CONFIG>)
  if(NOT rule STREQUAL "CONTROL")
    set_tests_properties(app_rules_${rule_lower} PROPERTIES WILL_FAIL TRUE)
  endif()
endforeach()

# ---- the terminal-only session path (sim open / press / state / shot / close) ----
add_test(NAME cli_session_flow
         COMMAND ${CMAKE_COMMAND} -DSIM=$<TARGET_FILE:sim> -DSRC=${CMAKE_SOURCE_DIR}/sim -DWORK=${CMAKE_BINARY_DIR}/cli_flow
                 -P ${CMAKE_SOURCE_DIR}/tests/cli_flow.cmake)

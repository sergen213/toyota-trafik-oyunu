cmake_minimum_required(VERSION 3.28)
project(ToyotaTrafikOyunuNative VERSION 0.1.0 LANGUAGES C CXX)

# C++26 is technically complete in WG21 but ISO 14882:2026 is still a DIS
# (see docs/ENVIRONMENT.md), so the project uses C++23.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(TTO_BUILD_TESTS "Build unit tests" ON)

find_package(SDL3 REQUIRED CONFIG)
find_package(SDL3_ttf REQUIRED CONFIG)
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MPG123 REQUIRED IMPORTED_TARGET libmpg123)
pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)
include(cmake/ODE.cmake)
find_package(glm CONFIG REQUIRED)
find_package(nlohmann_json REQUIRED)
find_path(STB_INCLUDE_DIR stb_image.h PATH_SUFFIXES stb REQUIRED)

add_compile_options(-Wall -Wextra -Wpedantic)

# Format parsers and game rules: no graphics/audio dependencies, unit-testable.
add_library(tto_core STATIC
  src/core/fs.cpp
  src/core/log.cpp
  src/data/vdd.cpp
  src/data/sorubank.cpp
  src/data/userdata.cpp
  src/data/mapdata.cpp
  src/formats/tds.cpp
  src/formats/smd.cpp
  src/formats/dfm.cpp
  src/data/validate.cpp
  src/game/vehicle.cpp
  src/game/traffic.cpp
  src/game/rules.cpp
  src/game/quiz.cpp
)
target_include_directories(tto_core PUBLIC src)
target_link_libraries(tto_core PUBLIC tto_ode glm::glm nlohmann_json::nlohmann_json)
target_compile_definitions(tto_core PUBLIC GLM_ENABLE_EXPERIMENTAL)

option(TTO_BUILD_GAME "Build the game executable" ON)
if(TTO_BUILD_GAME)
add_executable(toyota-trafik
  src/main.cpp
  src/app/app.cpp
  src/app/scene.cpp
  src/app/menu.cpp
  src/app/game.cpp
  src/app/exe_strings.cpp
  src/platform/audio.cpp
  src/render/image.cpp
  src/render/gl.cpp
  src/render/text.cpp
)
target_include_directories(toyota-trafik PRIVATE ${STB_INCLUDE_DIR})
target_link_libraries(toyota-trafik PRIVATE tto_core SDL3::SDL3 SDL3_ttf::SDL3_ttf
  OpenGL::GL GLEW::GLEW PkgConfig::MPG123 PkgConfig::FONTCONFIG)

install(TARGETS toyota-trafik RUNTIME DESTINATION bin)
endif()

if(TTO_BUILD_TESTS)
  enable_testing()
  find_package(GTest REQUIRED)
  add_executable(tto_tests
    tests/test_vdd.cpp
    tests/test_sorubank.cpp
    tests/test_userdata.cpp
    tests/test_tds.cpp
    tests/test_smd.cpp
    tests/test_rules.cpp
    tests/test_vehicle.cpp
    tests/test_realdata.cpp
    tests/test_dfm.cpp
  )
  target_link_libraries(tto_tests PRIVATE tto_core GTest::gtest_main)
  target_compile_definitions(tto_tests PRIVATE TTO_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  include(GoogleTest)
  gtest_discover_tests(tto_tests)
endif()

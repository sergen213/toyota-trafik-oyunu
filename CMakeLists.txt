cmake_minimum_required(VERSION 3.28)
project(ToyotaTrafikOyunuNative VERSION 0.2.0 LANGUAGES C CXX)

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
find_package(Vulkan REQUIRED)
find_program(GLSLC glslc REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MPG123 REQUIRED IMPORTED_TARGET libmpg123)
pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswscale libswresample)
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
  src/data/import.cpp
  src/game/traffic.cpp
  src/game/world.cpp
  src/game/rules.cpp
  src/game/quiz.cpp
)
target_include_directories(tto_core PUBLIC src)
find_package(LibArchive REQUIRED)
target_link_libraries(tto_core PUBLIC tto_ode glm::glm nlohmann_json::nlohmann_json LibArchive::LibArchive)
target_compile_definitions(tto_core PUBLIC GLM_ENABLE_EXPERIMENTAL)

option(TTO_BUILD_GAME "Build the game executable" ON)
if(TTO_BUILD_GAME)
add_executable(toyota-trafik
  src/main.cpp
  src/app/app.cpp
  src/app/scene.cpp
  src/app/menu.cpp
  src/app/game.cpp
  src/app/playlist.cpp
  src/app/exe_strings.cpp
  src/platform/audio.cpp
  src/platform/video.cpp
  src/render/image.cpp
  src/render/renderer.cpp
  src/render/gl.cpp
  src/render/vk.cpp
  src/render/text.cpp
)
# SPIR-V for the Vulkan backend, embedded as uint32 initializer lists.
set(TTO_SHADER_DIR ${CMAKE_SOURCE_DIR}/src/render/shaders)
set(TTO_SPV_DIR ${CMAKE_BINARY_DIR}/shaders)
set(TTO_SPV)
foreach(sh mesh.vert mesh.frag quad.vert quad.frag mesh_rt.frag mesh_m.frag mesh_rt_m.frag)
  set(src ${sh})
  set(defs)
  if(sh MATCHES "^mesh_")
    set(src mesh.frag)
    if(sh MATCHES "rt")
      list(APPEND defs -DRT)
    endif()
    if(sh MATCHES "_m\\.frag$")
      list(APPEND defs -DMOTION)
    endif()
  endif()
  add_custom_command(OUTPUT ${TTO_SPV_DIR}/${sh}.inc
    COMMAND ${CMAKE_COMMAND} -E make_directory ${TTO_SPV_DIR}
    COMMAND ${GLSLC} --target-env=vulkan1.3 -O ${defs} -mfmt=num ${TTO_SHADER_DIR}/${src} -o ${TTO_SPV_DIR}/${sh}.inc
    DEPENDS ${TTO_SHADER_DIR}/${src} ${TTO_SHADER_DIR}/common.glsl VERBATIM)
  list(APPEND TTO_SPV ${TTO_SPV_DIR}/${sh}.inc)
endforeach()
add_custom_target(tto_shaders DEPENDS ${TTO_SPV})
# AMD FidelityFX SDK v1.1.4 subset: FSR 3.1.4 upscaler + frame interpolation + optical flow, Vulkan backend (MIT,
# third_party/FidelityFX-SDK/PROVENANCE.txt). tools/ffx_fsr3_blobs.py compiles the passes with glslc (no FidelityFX_SC).
find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
set(TTO_FFX ${CMAKE_SOURCE_DIR}/third_party/FidelityFX-SDK)
set(TTO_FFX_BLOBS ${CMAKE_BINARY_DIR}/generated/tto_fsr3_blobs.h)
file(GLOB_RECURSE TTO_FFX_GPU CONFIGURE_DEPENDS ${TTO_FFX}/sdk/include/FidelityFX/gpu/*.h ${TTO_FFX}/sdk/src/backends/vk/shaders/*.glsl)
add_custom_command(OUTPUT ${TTO_FFX_BLOBS}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/generated
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/ffx_fsr3_blobs.py --glslc ${GLSLC} --sdk ${TTO_FFX} --output ${TTO_FFX_BLOBS}
  DEPENDS ${CMAKE_SOURCE_DIR}/tools/ffx_fsr3_blobs.py ${TTO_FFX_GPU} VERBATIM)
set(TTO_FFX_SOURCES
  ${TTO_FFX}/sdk/src/components/fsr3upscaler/ffx_fsr3upscaler.cpp
  ${TTO_FFX}/sdk/src/components/frameinterpolation/ffx_frameinterpolation.cpp
  ${TTO_FFX}/sdk/src/components/opticalflow/ffx_opticalflow.cpp
  ${TTO_FFX}/sdk/src/backends/vk/ffx_vk.cpp ${TTO_FFX}/sdk/src/shared/ffx_assert.cpp
  ${TTO_FFX}/sdk/src/shared/ffx_message.cpp ${TTO_FFX}/sdk/src/shared/ffx_object_management.cpp
  ${TTO_FFX}/sdk/src/shared/ffx_breadcrumbs_list.cpp)
add_library(tto_fsr3 STATIC ${TTO_FFX_SOURCES} src/render/fsr.cpp ${TTO_FFX_BLOBS})
set_source_files_properties(${TTO_FFX_SOURCES} PROPERTIES COMPILE_OPTIONS -w)
target_include_directories(tto_fsr3 SYSTEM PRIVATE ${TTO_FFX}/sdk/include ${TTO_FFX}/sdk/src/shared ${TTO_FFX}/sdk/src/backends/shared
  ${TTO_FFX}/sdk/src/components ${CMAKE_BINARY_DIR}/generated)
target_include_directories(tto_fsr3 PUBLIC src)
target_compile_options(tto_fsr3 PRIVATE -include ${CMAKE_SOURCE_DIR}/third_party/ffx-posix/ffx_posix.h)
target_link_libraries(tto_fsr3 PUBLIC Vulkan::Vulkan)
target_link_libraries(toyota-trafik PRIVATE tto_fsr3)
install(FILES ${TTO_FFX}/LICENSE.txt DESTINATION share/doc/toyota-trafik/third_party RENAME FidelityFX-SDK-LICENSE.txt)
add_dependencies(toyota-trafik tto_shaders)
target_include_directories(toyota-trafik PRIVATE ${STB_INCLUDE_DIR} ${TTO_SPV_DIR})
target_link_libraries(toyota-trafik PRIVATE tto_core SDL3::SDL3 SDL3_ttf::SDL3_ttf
  OpenGL::GL GLEW::GLEW Vulkan::Vulkan PkgConfig::MPG123 PkgConfig::FONTCONFIG PkgConfig::FFMPEG)
if(WIN32)
  # No console window when double-clicked; validation problems show a message box instead.
  set_target_properties(toyota-trafik PROPERTIES WIN32_EXECUTABLE ON)
  target_sources(toyota-trafik PRIVATE data/toyota-trafik.rc)
endif()

install(TARGETS toyota-trafik RUNTIME DESTINATION bin)
# The binary finds maps.json in <bindir>/data or <prefix>/share/toyota-trafik.
configure_file(data/maps.json ${CMAKE_BINARY_DIR}/data/maps.json COPYONLY)
install(FILES data/maps.json DESTINATION share/toyota-trafik)
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
    tests/test_world.cpp
    tests/test_import.cpp
  )
  target_link_libraries(tto_tests PRIVATE tto_core GTest::gtest_main)
  target_compile_definitions(tto_tests PRIVATE TTO_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  include(GoogleTest)
  gtest_discover_tests(tto_tests)
endif()

if(TTO_BUILD_TESTS AND TTO_BUILD_GAME)
  foreach(s talim_playthrough talim_pass playlist_editor map1 map2 map3 map4 map5)
    add_test(NAME playthrough_${s}
      COMMAND ${CMAKE_SOURCE_DIR}/tests/run_playthrough.sh $<TARGET_FILE:toyota-trafik>
              ${CMAKE_SOURCE_DIR}/tests/scripts/${s}.txt ${CMAKE_BINARY_DIR}/playthrough/${s})
    set_tests_properties(playthrough_${s} PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1800)
  endforeach()
  # Renderer variants: OpenGL fallback, and Vulkan with FSR 3.1.4 upscaling + frame generation.
  add_test(NAME playthrough_map3_gl COMMAND ${CMAKE_SOURCE_DIR}/tests/run_playthrough.sh $<TARGET_FILE:toyota-trafik>
           ${CMAKE_SOURCE_DIR}/tests/scripts/map3.txt ${CMAKE_BINARY_DIR}/playthrough/map3_gl --renderer gl)
  add_test(NAME playthrough_talim_fsr_framegen COMMAND ${CMAKE_SOURCE_DIR}/tests/run_playthrough.sh $<TARGET_FILE:toyota-trafik>
           ${CMAKE_SOURCE_DIR}/tests/scripts/talim_playthrough.txt ${CMAKE_BINARY_DIR}/playthrough/talim_fsr --fsr balanced --framegen)
  set_tests_properties(playthrough_map3_gl playthrough_talim_fsr_framegen PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1800)
endif()
if(TTO_BUILD_GAME)
  # Vulkan structs are zero-initialised with only sType given ({VK_STRUCTURE_TYPE_...}).
  set_source_files_properties(src/render/vk.cpp PROPERTIES COMPILE_OPTIONS -Wno-missing-field-initializers)
endif()

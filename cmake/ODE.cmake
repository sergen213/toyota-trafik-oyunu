include_guard(GLOBAL)
# The original game simulates cars with oxGLODE (ODE). We use ODE too:
# the system package when present, else the pinned upstream source tarball
# (odedevs/ode 0.16.6, commit a5fc0bc2, BSD licence: third_party/ode-LICENSE-BSD.TXT).
find_package(PkgConfig REQUIRED)
pkg_check_modules(SYSTEM_ODE IMPORTED_TARGET ode>=0.16)
if(SYSTEM_ODE_FOUND)
  add_library(tto_ode INTERFACE)
  target_link_libraries(tto_ode INTERFACE PkgConfig::SYSTEM_ODE)
  message(STATUS "ODE: system ${SYSTEM_ODE_VERSION}")
  return()
endif()

include(FetchContent)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(ODE_DOUBLE_PRECISION OFF CACHE BOOL "" FORCE)
set(ODE_WITH_DEMOS OFF CACHE BOOL "" FORCE)
set(ODE_WITH_TESTS OFF CACHE BOOL "" FORCE)
set(ODE_WITH_OPCODE ON CACHE BOOL "" FORCE)
set(ODE_NO_TRIMESH OFF CACHE BOOL "" FORCE)
set(ODE_WITH_GIMPACT OFF CACHE BOOL "" FORCE)
set(ODE_WITH_LIBCCD OFF CACHE BOOL "" FORCE)
if(NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM)
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()
FetchContent_Declare(tto_ode_src
  URL "${CMAKE_CURRENT_LIST_DIR}/../third_party/ode-0.16.6.tar.gz"
  URL_HASH SHA256=05c0a0ea84c58c213d05e220adbc6559c473227505d5668a51493eed21b6c2f8
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(tto_ode_src)
add_library(tto_ode INTERFACE)
target_link_libraries(tto_ode INTERFACE ODE)
message(STATUS "ODE: bundled 0.16.6")

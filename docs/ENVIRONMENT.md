# Development environment

Recorded 2026-10-25 on the primary development machine.

| Component | Version |
|---|---|
| OS | CachyOS (Arch-based), kernel 7.3.0-4-cachyos x86_64 |
| CPU / GPU | AMD Ryzen 9 9950X3D / AMD Radeon RX 7900 (Navi 31), Mesa 26.2.5 (GL 4.6 core) |
| Compilers | GCC 16.2.1, Clang 23.1.3 |
| Build | CMake 4.4.4, Ninja 1.13.2 |
| VCS / scripting | Git 2.56.0, Python 3.14.8 |
| Libraries | SDL3 3.4.18, SDL3_ttf 3.2.2, GLEW 2.3.1, GLM 1.0.3, libmpg123 1.33.7, fontconfig 2.18.3, stb (2026-04-15 snapshot), GoogleTest 1.18.0 |
| Reverse engineering | Ghidra 12.1.2 (`/opt/ghidra`), GNU objdump 2.47, Python `pefile` 2024.8.26, tesseract (eng) |
| Diagnostics only | Wine 11.20 (isolated prefix under `work/wine/`), Xvfb, xdotool, ImageMagick, FFmpeg 9.0.2 |

## C++ standard decision

WG21 completed C++26 technically in March 2026, but ISO/IEC 14882 edition 8 is still listed by ISO as a
**Draft International Standard** ("Under development", <https://www.iso.org/standard/91179.html>) at the time of
writing. Because the task requires an *officially finalized* standard, the project uses **C++23**
(`CMAKE_CXX_STANDARD 23`, no extensions). GCC 16 and Clang 23 both implement the C++23 features used
(`std::format`, `std::span`, `std::from_chars` for floats, `std::erase`). Revisit when ISO publishes 14882:2026.

Re-verified 2026-10-26: ISO still lists *ISO/IEC DIS 14882* in the enquiry phase, and WG21 paper P4388R0 ("Ship C++26",
October 2026 mailing) proposes submitting it for publication, so it is not yet published. Both compilers report
`__cplusplus 202400L` for `-std=c++26` (partial/draft). `scripts/check_toolchain.sh` compiles and runs
`scripts/cxx23_probe.cpp` (`std::expected`, `std::print`, `std::ranges::to`, float `from_chars`) with both GCC 16 and
Clang 23 in `-std=c++23` mode: both pass.

## Pinned dependency policy

Only distribution packages are used; no vendored third-party code except the header-only stb from the
system `stb` package. Minimum versions are enforced by `find_package`/`pkg_check_modules` in `CMakeLists.txt`.

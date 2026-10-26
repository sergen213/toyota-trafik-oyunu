# Building and installing (CachyOS / Arch Linux)

## Dependencies (distribution packages)

```sh
sudo pacman -S --needed base-devel cmake ninja git sdl3 sdl3_ttf glew glm mpg123 fontconfig stb nlohmann-json gtest noto-fonts \
    vulkan-headers vulkan-icd-loader shaderc ffmpeg python
# optional: vulkan-validation-layers (VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation)
```

ODE is not packaged with trimesh support on Arch. CMake therefore uses the system `ode` when pkg-config finds it,
and otherwise builds the pinned source tarball `third_party/ode-0.16.6.tar.gz` (SHA-256 checked, BSD licence).
No network access is needed. Versions used during development are listed in `ENVIRONMENT.md`.

## Build

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja -C build
```

Options: `-DTTO_BUILD_TESTS=OFF`, `-DTTO_BUILD_GAME=OFF` (parsers and tests only; no SDL/GL needed).

## Install

```sh
cmake --install build --prefix ~/.local   # installs bin/toyota-trafik and share/toyota-trafik/maps.json
```

The binary looks for `maps.json` next to itself, in `../data` and in `../share/toyota-trafik`. Use `--data <dir>`
to override.

## Game data

Install the game with Wine or copy the folder straight from the CD:
`Kurulum2/program files/ToyotaSA/Toyota Trafik Oyunu v2/` (v2) or `Kurulum1/.../Toyota Trafik Oyunu/` (v1).
The CD folder can be used as-is, because the MSI only copies files. Pass the folder with `--game` or set
`TTO_GAME_DIR`. The game folder is only read, never written. Profiles live in `$XDG_DATA_HOME/toyota-trafik/UserData.dat`
(same byte format as the original).

## Running the original instead (reference / comparison)

```sh
scripts/wine_recover.sh setup "<game folder>" ~/tto-wine
scripts/wine_recover.sh run ~/tto-wine
```

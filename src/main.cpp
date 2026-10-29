// toyota-trafik: native Linux reimplementation of "Cancan ile Toyota Trafik Oyunu".
// Requires the user's own installed game folder (see docs/BUILDING.md).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <SDL3/SDL_main.h>

#include "app/app.hpp"
#include "data/import.hpp"
#include "data/validate.hpp"

namespace {
void usage() {
    std::puts(
        "usage: toyota-trafik --game <dir> [options]\n"
        "  --game <dir>     installed game folder (contains TrafikOyunu.exe, HG.vdd, Data/)\n"
        "  --import <path>  import the game from the setup CD folder (Kurulum1/2) or its .rar into the profile folder\n"
        "  --data <dir>     project data dir with maps.json (default: next to the binary / install share)\n"
        "  --check          validate the game folder and exit (0 = ok, 2 = problems)\n"
        "  --save <dir>     profile directory (default: SDL pref path, e.g. ~/.local/share/toyota-trafik)\n"
        "  --fullscreen     start fullscreen (F11 toggles)\n"
        "  --size WxH       window size (default 1024x768)\n"
        "  --mute           no audio\n"
        "  --renderer <r>   auto (default: Vulkan, falls back to OpenGL), vulkan or gl\n"
        "  --no-rt          disable Vulkan ray-traced shadows\n"
        "  --fsr <q>        AMD FSR 3.1.4 upscaling (Vulkan): off, native, quality, balanced, performance, ultra\n"
        "  --framegen       FSR 3.1.4 frame generation (implies --fsr quality when FSR is off)\n"
        "  --no-enhanced    original look: no volumetric clouds or parallax relief (F7 toggles)\n"
        "  --script <file>  run an automated input script (tests), --shots <dir> for its screenshots\n"
        "  --fixed-step     simulate 1/60 s per frame (deterministic tests)\n"
        "  --seed <n>       random seed\n"
        "Environment: TTO_GAME_DIR is used when --game is not given.");
}

std::filesystem::path default_data_dir(const char* argv0) {
    namespace fs = std::filesystem;
    const char* sdlBase = SDL_GetBasePath();
    // SDL's base path ends in a separator; parent_path() strips it.
    const fs::path base = sdlBase ? fs::path(reinterpret_cast<const char8_t*>(sdlBase)).parent_path() : fs::path(argv0).parent_path();
    for (const fs::path& c : {base / "data", base.parent_path() / "data", base.parent_path() / "share/toyota-trafik"})
        if (fs::exists(c / "maps.json")) return c;
    return "data";
}

std::filesystem::path pref_dir() {
    char* pref = SDL_GetPrefPath("", "toyota-trafik");
    if (!pref) return {};
    std::filesystem::path p(reinterpret_cast<const char8_t*>(pref));
    SDL_free(pref);
    return p;
}

std::filesystem::path remembered_path() {
    const auto p = pref_dir();
    return p.empty() ? p : p / "gamedir.txt";
}

// Imports from the setup folder / .rar into <pref>/game; empty on failure (already reported).
std::filesystem::path import_into_pref(const std::filesystem::path& source, bool gui) {
    const auto dest = pref_dir() / "game";
    try {
        std::fprintf(stderr, "importing %s -> %s\n", source.string().c_str(), dest.string().c_str());
        tto::import_game(source, dest);
        return dest;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "import failed: %s\n", e.what());
        if (gui) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Toyota Trafik Oyunu", (std::string("Import failed: ") + e.what()).c_str(), nullptr);
        return {};
    }
}

void remember_game_dir(const std::filesystem::path& dir) {
    if (const auto p = remembered_path(); !p.empty()) std::ofstream(p) << reinterpret_cast<const char*>(dir.u8string().c_str());
}

bool usable(const std::filesystem::path& dir) { return !dir.empty() && tto::validate_game_dir(tto::GameFs(dir)).ok(); }

// No --game / TTO_GAME_DIR (double-clicked AppImage or .exe): last choice, default install folders, then a folder dialog.
std::filesystem::path find_game_dir() {
    namespace fs = std::filesystem;
    if (const auto p = remembered_path(); !p.empty()) {
        std::string s;
        std::ifstream in(p);
        std::getline(in, s);
        if (fs::path d(reinterpret_cast<const char8_t*>(s.c_str())); usable(d)) return d;
    }
    const fs::path rel = "ToyotaSA/Toyota Trafik Oyunu";
    std::vector<fs::path> cands = {"C:/Program Files (x86)" / rel, "C:/Program Files" / rel};
    if (const char* h = SDL_GetUserFolder(SDL_FOLDER_HOME))
        for (const char* pf : {".wine/drive_c/Program Files (x86)", ".wine/drive_c/Program Files"})
            cands.push_back(fs::path(reinterpret_cast<const char8_t*>(h)) / pf / rel);
    for (const auto& c : cands)
        if (usable(c)) return c;

    if (!SDL_Init(SDL_INIT_VIDEO)) return {};
    // Choose the source: an installed game folder (used in place), or the original setup media (imported once).
    const SDL_MessageBoxButtonData buttons[] = {{SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 0, "Installed game folder"},
                                                {0, 1, "Setup folder (CD)"},
                                                {0, 2, "Setup .rar file"},
                                                {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 3, "Cancel"}};
    const SDL_MessageBoxData box{SDL_MESSAGEBOX_INFORMATION, nullptr, "Toyota Trafik Oyunu",
                                 "Where is your copy of the game?\n\n"
                                 "Installed game folder: the folder containing TrafikOyunu.exe.\n"
                                 "Setup folder: the CD (or a copy of it) with Kurulum1/Kurulum2.\n"
                                 "Setup .rar: an archive of that CD.\n\n"
                                 "Setup media is imported once into the profile folder.",
                                 4, buttons, nullptr};
    int choice = 3;
    SDL_ShowMessageBox(&box, &choice);
    struct Pick {
        bool done = false;
        fs::path path;
    } pick;
    auto cb = [](void* ud, const char* const* list, int) {
        auto* r = static_cast<Pick*>(ud);
        if (list && list[0]) r->path = fs::path(reinterpret_cast<const char8_t*>(list[0]));
        r->done = true;
    };
    static const SDL_DialogFileFilter rar[] = {{"RAR archive", "rar"}};
    if (choice == 0 || choice == 1) SDL_ShowOpenFolderDialog(cb, &pick, nullptr, nullptr, false);
    else if (choice == 2) SDL_ShowOpenFileDialog(cb, &pick, nullptr, rar, 1, nullptr, false);
    else pick.done = true;
    while (!pick.done) {
        SDL_PumpEvents();
        SDL_Delay(20);
    }
    fs::path dir = pick.path;
    // A setup folder picked as "installed folder" is imported too.
    if (!dir.empty() && (choice != 0 || !usable(dir))) dir = import_into_pref(dir, true);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return dir;
}
}  // namespace

int main(int argc, char** argv) {
    tto::Options o;
    bool check = false;
    if (const char* g = std::getenv("TTO_GAME_DIR")) o.gameDir = g;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s needs a value\n", a.c_str());
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--game") o.gameDir = next();
        else if (a == "--import") {
            o.gameDir = import_into_pref(next(), false);
            if (o.gameDir.empty()) return 2;
        } else if (a == "--data") o.dataDir = next();
        else if (a == "--save") o.saveDir = next();
        else if (a == "--script") o.script = next();
        else if (a == "--shots") o.shotDir = next();
        else if (a == "--seed") o.seed = static_cast<unsigned>(std::stoul(next()));
        else if (a == "--size") {
            const std::string s = next();
            if (std::sscanf(s.c_str(), "%dx%d", &o.width, &o.height) != 2) { usage(); return 2; }
        } else if (a == "--fullscreen") o.fullscreen = true;
        else if (a == "--mute") o.mute = true;
        else if (a == "--renderer") o.renderer = next();
        else if (a == "--no-rt") o.rayTracing = false;
        else if (a == "--no-enhanced") o.enhanced = false;
        else if (a == "--fsr") {
            static const char* q[] = {"native", "quality", "balanced", "performance", "ultra"};
            const std::string v = next();
            o.fsr = -1;
            for (int k = 0; k < 5; ++k)
                if (v == q[k]) o.fsr = k;
            if (o.fsr < 0 && v != "off") { usage(); return 2; }
        } else if (a == "--framegen") o.frameGen = true;
        else if (a == "--fixed-step") o.fixedStep = true;
        else if (a == "--check") check = true;
        else if (a == "-h" || a == "--help") { usage(); return 0; }
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); usage(); return 2; }
    }
    if (o.gameDir.empty()) o.gameDir = find_game_dir();
    if (o.gameDir.empty()) {
        usage();
        return 2;
    }
    if (o.dataDir.empty()) o.dataDir = default_data_dir(argv[0]);
    const auto rep = tto::validate_game_dir(tto::GameFs(o.gameDir));
    if (check || !rep.ok()) {
        std::fputs(rep.text().c_str(), rep.ok() ? stdout : stderr);
        if (!rep.ok()) {
            if (!check) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Toyota Trafik Oyunu", rep.text().c_str(), nullptr);
            return 2;
        }
        return 0;
    }
    remember_game_dir(o.gameDir);
    tto::App app(std::move(o));
    return app.run();
}

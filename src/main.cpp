// toyota-trafik: native Linux reimplementation of "Cancan ile Toyota Trafik Oyunu".
// Requires the user's own installed game folder (see docs/BUILDING.md).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "app/app.hpp"
#include "data/validate.hpp"

namespace {
void usage() {
    std::puts(
        "usage: toyota-trafik --game <dir> [options]\n"
        "  --game <dir>     installed game folder (contains TrafikOyunu.exe, HG.vdd, Data/)\n"
        "  --data <dir>     project data dir with maps.json (default: next to the binary / install share)\n"
        "  --check          validate the game folder and exit (0 = ok, 2 = problems)\n"
        "  --save <dir>     profile directory (default: $XDG_DATA_HOME/toyota-trafik)\n"
        "  --fullscreen     start fullscreen (F11 toggles)\n"
        "  --size WxH       window size (default 1024x768)\n"
        "  --mute           no audio\n"
        "  --script <file>  run an automated input script (tests), --shots <dir> for its screenshots\n"
        "  --fixed-step     simulate 1/60 s per frame (deterministic tests)\n"
        "  --seed <n>       random seed\n"
        "Environment: TTO_GAME_DIR is used when --game is not given.");
}

std::filesystem::path default_data_dir(const char* argv0) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path exe = fs::canonical("/proc/self/exe", ec);
    const fs::path base = ec ? fs::path(argv0).parent_path() : exe.parent_path();
    for (const fs::path& c : {base / "data", base.parent_path() / "data", base.parent_path() / "share/toyota-trafik"})
        if (fs::exists(c / "maps.json")) return c;
    return "data";
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
        else if (a == "--data") o.dataDir = next();
        else if (a == "--save") o.saveDir = next();
        else if (a == "--script") o.script = next();
        else if (a == "--shots") o.shotDir = next();
        else if (a == "--seed") o.seed = static_cast<unsigned>(std::stoul(next()));
        else if (a == "--size") {
            const std::string s = next();
            if (std::sscanf(s.c_str(), "%dx%d", &o.width, &o.height) != 2) { usage(); return 2; }
        } else if (a == "--fullscreen") o.fullscreen = true;
        else if (a == "--mute") o.mute = true;
        else if (a == "--fixed-step") o.fixedStep = true;
        else if (a == "--check") check = true;
        else if (a == "-h" || a == "--help") { usage(); return 0; }
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); usage(); return 2; }
    }
    if (o.gameDir.empty()) {
        usage();
        return 2;
    }
    if (o.dataDir.empty()) o.dataDir = default_data_dir(argv[0]);
    const auto rep = tto::validate_game_dir(tto::GameFs(o.gameDir));
    if (check || !rep.ok()) {
        std::fputs(rep.text().c_str(), rep.ok() ? stdout : stderr);
        if (!rep.ok()) return 2;
        return 0;
    }
    tto::App app(std::move(o));
    return app.run();
}

#include "data/validate.hpp"

#include <format>

#include "data/sorubank.hpp"
#include "data/vdd.hpp"
#include "formats/dfm.hpp"
#include "formats/smd.hpp"
#include "formats/tds.hpp"

namespace tto {

std::string ValidationReport::text() const {
    std::string s = std::format("Game data version: {}\n", version);
    for (const auto& m : missing) s += "MISSING  " + m + "\n";
    for (const auto& m : invalid) s += "INVALID  " + m + "\n";
    s += ok() ? "OK: all required files present and readable.\n" : "Game data is incomplete or damaged.\n";
    return s;
}

ValidationReport validate_game_dir(const GameFs& fs) {
    ValidationReport r;
    // Files the native game needs for menus, the licence quiz and map 1 (Eğitim Sahası).
    static const char* required[] = {
        "TrafikOyunu.exe", "HG.vdd", "Data\\SoruBank.dat",
        "maps\\sehir01C.jpg", "maps\\giris.jpg", "maps\\zemin01_k.jpg", "maps\\zemin02_k.jpg", "maps\\direksiyon_k.bmp",
        "maps\\lambakirmizi.bmp", "maps\\lambasari.bmp", "maps\\lambayesil.bmp", "maps\\Cerceve.jpg",
        "Bolumler\\Diger\\Talim.jpg", "Bolumler\\Diger\\Lamba.3DS", "Bolumler\\Diger\\GokYuzu\\Ust.jpg",
        "Arabalar\\YARIS\\YARIS.smd", "Arabalar\\motor.wav", "Arabalar\\korna.wav",
        "sesler\\trafik_ses2.mp3", "sesler\\crash1.wav",
    };
    for (const char* f : required)
        if (!fs.find(f)) r.missing.push_back(f);
    auto check = [&](const char* f, auto&& fn) {
        if (auto p = fs.find(f)) {
            try {
                fn(*p);
            } catch (const std::exception& e) {
                r.invalid.push_back(std::string(f) + ": " + e.what());
            }
        }
    };
    check("HG.vdd", [](const auto& p) { (void)parse_vdd(read_text(p)); });
    check("Data\\SoruBank.dat", [](const auto& p) {
        if (parse_sorubank(read_file(p)).size() < 150) throw std::runtime_error("fewer than 150 questions");
    });
    check("Arabalar\\YARIS\\YARIS.smd", [](const auto& p) { (void)parse_smd(read_text(p)); });
    check("Bolumler\\Diger\\Lamba.3DS", [](const auto& p) { (void)load_3ds(read_file(p)); });
    r.version = "unknown";
    check("TrafikOyunu.exe", [&](const auto& p) {
        const auto res = pe_rcdata(read_file(p));
        if (!res.contains("TGIRISFORM") || !res.contains("TFORMOYUN")) throw std::runtime_error("not the Toyota Trafik Oyunu executable");
        const auto oyun = parse_dfm(res.at("TFORMOYUN"));
        // v2 ("Hızlı Bilgisayarlar İçin") enables 2x anti-aliasing (docs/GAME_IDENTITY.md).
        const auto* v = oyun.find("GLFullScreenViewer");
        r.version = v && v->text("Buffer.AntiAliasing") == "aa2xHQ" ? "v2" : "v1";
    });
    return r;
}

}  // namespace tto

// Start screen, help, driver profiles, licence quiz and map selection (TGirisForm).
// Layout: positions measured from the original running under Wine at 1024x768
// (work/runs/recover_verified) and the TGirisForm DFM in the user's executable;
// glyphs, captions and pictures are read from the user's game files at runtime.
#include <GL/glew.h>

#include <algorithm>
#include <format>

#include "app/app.hpp"
#include "core/log.hpp"

namespace tto {
namespace {

glm::vec3 tcolor(int bgr) { return {static_cast<float>(bgr & 0xFF) / 255.0f, static_cast<float>((bgr >> 8) & 0xFF) / 255.0f, static_cast<float>((bgr >> 16) & 0xFF) / 255.0f}; }
const glm::vec3 kPanel = tcolor(13676172);  // panel and title-bar colour
const glm::vec3 kListBg = tcolor(15662544);
const glm::vec3 kLabelBlue = tcolor(16744448);
constexpr glm::vec2 kPanel2{368, 481};       // driver list / new-driver panel
constexpr glm::vec2 kHelp{64, 44};           // paGiris
constexpr glm::vec2 kMapSel{280, 480};       // paHaritaSecim
constexpr glm::vec4 kHelpView{70, 50, 885, 593};  // ScrollBox1 client area

bool inside(glm::vec2 p, float x, float y, float w, float h) { return p.x >= x && p.y >= y && p.x < x + w && p.y < y + h; }

std::size_t cp_count(const std::string& s) {
    std::size_t n = 0;
    for (unsigned char c : s) n += (c & 0xC0) != 0x80;
    return n;
}

// Original 0x51770c: wrap at the last space before column 55, at most two lines.
std::vector<std::string> wrap55(const std::string& s) {
    if (cp_count(s) <= 55) return {s};
    std::size_t cps = 0, cut = std::string::npos;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) ++cps;
        if (cps > 55) break;
        if (s[i] == ' ') cut = i;
    }
    if (cut == std::string::npos) return {s};
    return {s.substr(0, cut), s.substr(cut + 1)};
}

}  // namespace

const Texture* App::image(const std::string& winPath, bool key) {
    const std::string id = winPath + (key ? "#k" : "");
    if (auto it = glyphs_.find(id); it != glyphs_.end()) return it->second;
    const Texture* t = nullptr;
    if (auto p = fs_->find(winPath)) {
        try {
            Image img = load_image(*p);
            if (key) key_bottom_left(img);
            t = r_.texture_image(id, img);
        } catch (const std::exception& e) {
            log_warn("{}", e.what());
        }
    } else {
        log_warn("missing {}", winPath);
    }
    return glyphs_[id] = t;
}

const Texture* App::glyph(const std::string& obj, int index, int count) {
    const std::string id = "glyph:" + obj + "#" + std::to_string(index);
    if (auto it = glyphs_.find(id); it != glyphs_.end()) return it->second;
    const Texture* t = nullptr;
    if (const DfmObject* o = giris_.find(obj)) {
        const auto data = o->image("Glyph.Data");
        if (!data.empty()) {
            try {
                Image full = load_image_mem(data.data(), data.size());
                Image img;
                img.w = full.w / count;
                img.h = full.h;
                img.rgba.resize(static_cast<std::size_t>(img.w) * img.h * 4);
                for (int y = 0; y < img.h; ++y)
                    std::copy_n(full.rgba.data() + (static_cast<std::size_t>(y) * full.w + static_cast<std::size_t>(index * img.w)) * 4,
                                static_cast<std::size_t>(img.w) * 4, img.rgba.data() + static_cast<std::size_t>(y) * img.w * 4);
                key_bottom_left(img);
                t = r_.texture_image(id, img);
            } catch (const std::exception& e) {
                log_warn("glyph {}: {}", obj, e.what());
            }
        }
    }
    return glyphs_[id] = t;
}

void App::menu_load() {
    formMsg_ = exe_->ansi_or(0x517a70, "Lütfen formu doldurun...");
    screen_ = Screen::Title;
}

void App::goto_screen(Screen s) {
    screen_ = s;
    screenTime_ = 0;
    popupOpen_ = false;
}

void App::message_box(std::string text, std::string caption) {
    modal_ = std::move(text);
    modalCaption_ = std::move(caption);
}

void App::draw_bar(float x, float y, float w, float h, const std::string& t, glm::vec3 bg, glm::vec3 fg, TextRenderer::Font f) {
    r_.quad(x, y, w, h, nullptr, glm::vec4(bg, 1));
    text_.draw(t, x + w / 2, y + (h - text_.line_height(f)) / 2, f, glm::vec4(fg, 1), 2);
}

void App::draw_button(const DfmObject& b, glm::vec2 o, bool enabled) {
    const float x = o.x + static_cast<float>(b.num("Left")), y = o.y + static_cast<float>(b.num("Top"));
    const float w = static_cast<float>(b.num("Width")), h = static_cast<float>(b.num("Height"));
    const int n = std::max(1, b.num("NumGlyphs", 1));
    const Texture* g = glyph(b.name, enabled || n < 2 ? 0 : 1, n);
    const bool hover = enabled && inside(mouse_, x, y, w, h);
    float gy = y + 3;
    if (g) {
        r_.quad(x + (w - static_cast<float>(g->w)) / 2, gy, static_cast<float>(g->w), static_cast<float>(g->h), g,
                enabled ? glm::vec4(1) : glm::vec4(1, 1, 1, 0.5f));
        gy += static_cast<float>(g->h) + 2;
    }
    std::string cap = b.text("Caption");
    // Runtime captions set in FormCreate (0x5177e0) replace the ASCII-folded DFM ones.
    static const std::pair<const char*, std::uint32_t> kCaps[] = {
        {"btHarita1", 0x517af0}, {"btHarita2", 0x517b08}, {"btHarita3", 0x517b18}, {"btHarita4", 0x517b28},
        {"SpeedButton3", 0x517b40}, {"SpeedButton4", 0x517b58}, {"SpeedButton6", 0x517b6c}, {"SpeedButton5", 0x517b80},
        {"SpeedButton13", 0x517b90}, {"SpeedButton12", 0x517ba8}};
    for (auto [name, va] : kCaps)
        if (b.name == name) cap = exe_->ansi_or(va, cap);
    if (!cap.empty())
        text_.draw(cap, x + w / 2, gy, TextRenderer::Font::UiBold, hover ? glm::vec4(0.1f, 0.1f, 0.6f, 1) : glm::vec4(0, 0, 0, enabled ? 1 : 0.5f), 2);
}

bool App::hit_button(const DfmObject& b, glm::vec2 o, glm::vec2 p) const {
    return inside(p, o.x + static_cast<float>(b.num("Left")), o.y + static_cast<float>(b.num("Top")), static_cast<float>(b.num("Width")),
                  static_cast<float>(b.num("Height")));
}

// ---------------------------------------------------------------- quiz

namespace {
struct QuizLine {
    std::string text;
    float y;
    int answer;  // -1 question, else 0..2
};
std::vector<QuizLine> quiz_layout(const AskedQuestion& a) {
    std::vector<QuizLine> out;
    float y = 20;
    auto qs = wrap55(a.q->text);
    for (std::size_t i = 0; i < qs.size(); ++i) {
        out.push_back({qs[i], y, -1});
        if (i + 1 < qs.size()) y += 20;
    }
    y += 30;
    static const char* pre[3] = {"a ) ", "b ) ", "c ) "};
    for (int i = 0; i < 3; ++i) {
        auto ls = wrap55(std::string(pre[i]) + a.line(i));
        for (const auto& l : ls) {
            out.push_back({l, y, i});
            y += 20;
        }
    }
    return out;
}
}  // namespace

std::optional<glm::vec4> App::quiz_line_rect(int line) const {
    if (!quiz_) return std::nullopt;
    const glm::vec2 img = panel1() + glm::vec2(49, 169);  // paVid.Image4
    std::optional<glm::vec4> r;
    for (const auto& l : quiz_layout(quiz_->current()))
        if (l.answer == line) {
            const glm::vec4 row{img.x, img.y + l.y, 598, 20};
            r = r ? glm::vec4(r->x, r->y, r->z, row.y + row.w - r->y) : row;
        }
    return r;
}

void App::start_quiz() {
    quiz_.emplace(bank_, needCorrect_, static_cast<std::uint32_t>(rng_()));
    quizFailedShown_ = false;
}

void App::quiz_click(int line) {
    if (!quiz_ || !profile_) return;
    if (quiz_->state() == LicenceQuiz::State::Failed) {
        quiz_->restart_round();  // original quirk: the next click starts a new round
        quizFailedShown_ = false;
        return;
    }
    if (quiz_->state() != LicenceQuiz::State::Asking) return;
    profile_->set_score(profile_->score() + quiz_->answer(line));
    if (quiz_->state() == LicenceQuiz::State::Passed) {
        licenceThisRun_ = true;
        profile_->set_licence(true);
        log_info("licence quiz passed ({} correct)", quiz_->correct());
    } else if (quiz_->state() == LicenceQuiz::State::Failed) {
        profile_->set_score(0);
        message_box(exe_->ansi_or(0x517380, "Oyuna baslamak icin gerekli sürücü belgesini alamadiniz. Lutfen tekrar deneyin."),
                    exe_->ansi_or(0x517378, "Uyari"));
    }
}

void App::try_start_map(int map) {
    if (!licenceThisRun_) {
        message_box(exe_->ansi_or(0x517eac, "Oyunu oynayabilmek için önce \"Sürücü Belgesi\" düğmesine basarak 10 sorudan ") +
                        std::to_string(needCorrect_) + exe_->ansi_or(0x517f00, " tanesini doğru yanıtlamalısın."),
                    exe_->ansi_or(0x517f20, "Uyarı"));
        return;
    }
    music_.reset();
    save_profile();
    start_map(map);
}

void App::save_profile() {
    if (!profile_) return;
    tto::save_profile(profiles_, *profile_, profile_->key(), session_total());
    try {
        const auto bytes = serialize_profiles(profiles_);
        const auto tmp = userDataPath_.string() + ".tmp";
        std::FILE* f = std::fopen(tmp.c_str(), "wb");
        if (!f) throw std::runtime_error("cannot write " + tmp);
        const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
        std::fclose(f);
        if (!ok) throw std::runtime_error("short write " + tmp);
        std::filesystem::rename(tmp, userDataPath_);  // atomic replace: no half-written profiles
    } catch (const std::exception& e) {
        log_error("saving profiles: {}", e.what());
    }
}

// ---------------------------------------------------------------- input

void App::menu_wheel(float dy) {
    if (screen_ == Screen::Help) helpScroll_ = std::clamp(helpScroll_ - dy * 60, 0.0f, 1760.0f);
}

void App::menu_text(const char* t) {
    if (screen_ != Screen::NewDriver || formFocus_ > 2) return;
    std::string& s = form_[formFocus_];
    if (cp_count(s) < 50) s += t;
}

void App::menu_key(SDL_Keycode k) {
    if (!modal_.empty()) {
        if (k == SDLK_RETURN || k == SDLK_ESCAPE || k == SDLK_SPACE) modal_.clear();
        return;
    }
    switch (screen_) {
        case Screen::Title:
            if (k == SDLK_RETURN || k == SDLK_SPACE) goto_screen(Screen::Help);
            break;
        case Screen::Help:
            if (k == SDLK_DOWN) helpScroll_ = std::min(helpScroll_ + 40, 1760.0f);
            if (k == SDLK_UP) helpScroll_ = std::max(helpScroll_ - 40, 0.0f);
            if (k == SDLK_RETURN || k == SDLK_ESCAPE) menu_click({502, 678}, false);
            break;
        case Screen::DriverList:
            if (k == SDLK_DOWN && selected_ + 1 < static_cast<int>(profiles_.size())) ++selected_;
            if (k == SDLK_UP && selected_ > 0) --selected_;
            if (k == SDLK_RETURN) menu_click(kPanel2 + glm::vec2(190, 236), false);
            break;
        case Screen::NewDriver:
            if (k == SDLK_TAB) formFocus_ = (formFocus_ + 1) % 3;
            if (k == SDLK_BACKSPACE && formFocus_ <= 2) {
                std::string& s = form_[formFocus_];
                while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) s.pop_back();
                if (!s.empty()) s.pop_back();
            }
            if (k == SDLK_RETURN) menu_click(kPanel2 + glm::vec2(96, 228), false);
            if (k == SDLK_ESCAPE) goto_screen(Screen::DriverList);
            break;
        case Screen::Main:
        case Screen::MapSelect:
            if (k == SDLK_ESCAPE) goto_screen(Screen::Main);
            break;
        default: break;
    }
}

void App::menu_click(glm::vec2 p, bool) {
    if (!modal_.empty()) {
        modal_.clear();
        return;
    }
    if (const DfmObject* q = giris_.find("SpeedButton2"); q && inside(p, 975, 718, 41, 41)) {
        quit_ = true;  // top-right exit glyph (ModalResult 2)
        return;
    }
    switch (screen_) {
        case Screen::Title:
            if (inside(p, 318, 470, 387, 280)) {
                goto_screen(Screen::Help);
                helpScroll_ = 0;
            }
            break;
        case Screen::Help:
            if (inside(p, 470, 647, 65, 65)) {  // DEVAM (SpeedButton7)
                introTime_ = 0;
                goto_screen(profile_ ? Screen::Main : Screen::DriverList);
                if (profile_) play_music("sesler\\trafik_ses2.mp3");
            }
            break;
        case Screen::DriverList: {
            const glm::vec2 o = kPanel2;
            if (inside(p, o.x + 8, o.y + 24, 273, 153)) {
                const int i = static_cast<int>((p.y - o.y - 24) / 20);
                if (i < static_cast<int>(profiles_.size())) selected_ = i;
            } else if (const DfmObject* b = giris_.find("RxSpeedButton1"); b && hit_button(*b, o, p)) {
                form_[0].clear();
                form_[1].clear();
                form_[2].clear();
                formAge_ = 12;
                formFocus_ = 0;
                formMsg_ = exe_->ansi_or(0x517a70, "Lütfen formu doldurun...");
                goto_screen(Screen::NewDriver);
            } else if (const DfmObject* t = giris_.find("SpeedButton10"); t && hit_button(*t, o, p) && selected_ >= 0 &&
                                                                          selected_ < static_cast<int>(profiles_.size())) {
                profile_ = profiles_[static_cast<std::size_t>(selected_)];
                if (profile_->score() < 0) profile_->set_score(0);
                needCorrect_ = profile_->licence() ? 3 : 8;
                goto_screen(Screen::Main);
            }
            break;
        }
        case Screen::NewDriver: {
            const glm::vec2 o = kPanel2;
            for (int i = 0; i < 3; ++i) {
                const float ey = i == 0 ? 40.0f : i == 1 ? 64.0f : 112.0f;
                if (inside(p, o.x + 72, o.y + ey, 209, 21)) formFocus_ = i;
            }
            if (inside(p, o.x + 72, o.y + 88, 41, 21)) formAge_ = formAge_ % 100 + 1;  // cb1: '1'..'100'
            if (const DfmObject* b = giris_.find("SpeedButton8"); b && hit_button(*b, o, p)) goto_screen(Screen::DriverList);
            if (const DfmObject* b = giris_.find("SpeedButton9"); b && hit_button(*b, o, p)) {
                // 0x516670: city, surname (bug: re-checks the name), name; last failing message wins.
                bool ok = true;
                if (form_[2].empty()) { formMsg_ = exe_->ansi_or(0x5169d4, "Lütfen oturduğunuz şehri girin"); ok = false; }
                if (form_[0].empty()) { formMsg_ = exe_->ansi_or(0x5169fc, "Lütfen soyadınızı girin"); ok = false; }
                if (form_[0].empty()) { formMsg_ = exe_->ansi_or(0x516a1c, "Lütfen adınızı girin"); ok = false; }
                if (ok) {
                    profile_ = new_profile(form_[0], form_[1], formAge_, form_[2]);
                    save_profile();  // assigns id = count+1
                    profile_ = profiles_.back();
                    needCorrect_ = 8;
                    goto_screen(Screen::Main);
                }
            }
            break;
        }
        case Screen::MapSelect: {
            for (int m = 1; m <= 4; ++m) {
                const DfmObject* b = giris_.find("btHarita" + std::to_string(m));
                const bool en = m == 1 || (profile_ && profile_->unlocked() >= m);
                if (b && en && hit_button(*b, kMapSel, p)) {
                    try_start_map(m);
                    return;
                }
            }
            if (!inside(p, kMapSel.x, kMapSel.y, 455, 113)) screen_ = Screen::Main;
            [[fallthrough]];
        }
        case Screen::Main: {
            const glm::vec2 o = panel1();
            auto hit = [&](const char* n) { const DfmObject* b = giris_.find(n); return b && hit_button(*b, o, p); };
            if (popupOpen_) {
                popupOpen_ = false;
                return;
            }
            if (hit("SpeedButton3")) {
                start_quiz();
            } else if (hit("SpeedButton4")) {
                quiz_.reset();
                goto_screen(Screen::MapSelect);
            } else if (hit("SpeedButton6")) {
                popupOpen_ = true;
            } else if (hit("SpeedButton5")) {
                if (auto b = fs_->find("bilgi.html")) SDL_OpenURL(("file://" + b->string()).c_str());
            } else if (hit("SpeedButton13")) {
                message_box("Müzik listesi düzenleyicisi bu sürümde yok: oyun playlist.m3u dosyasını kullanır.", "Bilgi");
            } else if (hit("SpeedButton12")) {  // Başa Dön (0x5173d0)
                quiz_.reset();
                profile_.reset();
                licenceThisRun_ = false;
                music_.reset();
                goto_screen(Screen::Title);
            } else if (screen_ == Screen::Main && quiz_) {
                if (quiz_->state() == LicenceQuiz::State::Failed && quizFailedShown_) {
                    quiz_click(0);
                    break;
                }
                for (int i = 0; i < 3; ++i)
                    if (auto r = quiz_line_rect(i); r && inside(p, r->x, r->y, r->z, r->w)) {
                        quiz_click(i);  // one answer per click: the next question's layout differs
                        break;
                    }
            }
            break;
        }
        default: break;
    }
}

// ---------------------------------------------------------------- drawing

void App::menu_draw() {
    using F = TextRenderer::Font;
    r_.begin_frame(vw_, vh_, {1, 1, 1});
    r_.begin_2d(1024, 768);
    const bool high = screen_ == Screen::Main || screen_ == Screen::MapSelect;
    const float hy = high ? -210.0f : -35.0f;
    // Intro animation (Timer1 0x515e60, 15 ms ticks): red -> yellow (tick 20) -> green (tick 50).
    if (introTime_ >= 0) introTime_ += 1.0 / 60.0;
    const double tick = introTime_ < 0 ? 0 : introTime_ / 0.015;
    const char* lamp = tick >= 50 ? "maps\\lambayesil.bmp" : tick >= 20 ? "maps\\lambasari.bmp" : "maps\\lambakirmizi.bmp";
    if (introTime_ >= 0 && tick >= 70 && !music_ && profile_) play_music("sesler\\trafik_ses2.mp3");
    if (const Texture* t = image("maps\\sehir01C.jpg")) r_.quad(92, hy, 840, 500, t);
    if (const Texture* t = image(lamp, true)) r_.quad(488, hy + 39, 71, 188, t);
    // Decorative cars (araba01..12.bmp) scroll along the street from tick 90.
    if (tick >= 90) {
        if (clock_ >= nextDeco_) {
            deco_.push_back({std::uniform_int_distribution<int>(1, 12)(rng_), -200});
            nextDeco_ = clock_ + std::uniform_real_distribution<double>(0.6, 1.6)(rng_);
        }
        for (auto& d : deco_) {
            d.x += 2.5f;
            if (const Texture* t = image(std::format("maps\\araba{:02}.bmp", d.img), true))
                r_.quad(92 + d.x, hy + 494 - static_cast<float>(t->h), static_cast<float>(t->w), static_cast<float>(t->h), t);
        }
        std::erase_if(deco_, [](const DecoCar& d) { return d.x > 860; });
    }
    r_.quad(0, 0, 92, 768, nullptr, glm::vec4(1));  // street ends at the picture edges
    r_.quad(932, 0, 92, 768, nullptr, glm::vec4(1));

    switch (screen_) {
        case Screen::Title: {
            text_.draw(exe_->ansi_or(0x517a48, "Başlamak için resme tıklayın..."), 512, 476, F::Ui, {0, 0, 0, 1}, 2);
            if (const Texture* t = image("maps\\giris_alt_s.jpg")) r_.quad(318, 493, 387, 253, t);
            break;
        }
        case Screen::Help: {
            r_.quad(kHelp.x, kHelp.y, 897, 681, nullptr, glm::vec4(kPanel, 1));
            r_.quad(kHelpView.x, kHelpView.y, kHelpView.z, kHelpView.w, nullptr, glm::vec4(kPanel, 1));
            // ScrollBox1 children with their DFM positions; texts from the executable.
            const glm::vec2 o{kHelpView.x, kHelpView.y - helpScroll_};
            glEnable(GL_SCISSOR_TEST);
            const float sx = static_cast<float>(vw_) / 1024.0f, sy = static_cast<float>(vh_) / 768.0f;
            glScissor(vx_ + static_cast<int>(kHelpView.x * sx), vy_ + static_cast<int>((768 - kHelpView.y - kHelpView.w) * sy),
                      static_cast<int>(kHelpView.z * sx), static_cast<int>(kHelpView.w * sy));
            static const std::pair<const char*, const char*> kImgs[] = {{"imGiris", "maps\\giris.jpg"}, {"imTrafik", "maps\\levha_puan.jpg"},
                {"imIsik", "maps\\ihlal_isik.jpg"}, {"imCop", "maps\\ihlal_cop.JPG"}, {"imPark", "maps\\ihlal_park.jpg"}, {"imYaya", "maps\\ihlal_yaya.jpg"}};
            for (auto [n, f] : kImgs)
                if (const DfmObject* d = giris_.find(n))
                    if (const Texture* t = image(f))
                        r_.quad(o.x + static_cast<float>(d->num("Left")), o.y + static_cast<float>(d->num("Top")), static_cast<float>(d->num("Width")),
                                static_cast<float>(d->num("Height")), t);
            static const std::pair<const char*, std::uint32_t> kTexts[] = {{"Label17", 0x51825c}, {"Label19", 0x518280}, {"Label13", 0x5183b4},
                {"Label10", 0x5183e8}, {"Label14", 0x5184d0}, {"Label15", 0x5185b8}, {"Label18", 0x5186a8}, {"Label16", 0x5186d4}};
            for (auto [n, va] : kTexts)
                if (const DfmObject* d = giris_.find(n)) {
                    const bool big = d->num("Font.Height") <= -16;
                    const float x = n == std::string("Label17") ? 512 - kHelpView.x : static_cast<float>(d->num("Left"));
                    text_.draw(exe_->ansi_or(va, d->text("Caption")), o.x + x, o.y + static_cast<float>(d->num("Top")), big ? F::Big : F::UiBold,
                               {0, 0, 0, 1}, n == std::string("Label17") ? 2 : 0);
                }
            static const std::pair<const char*, std::uint32_t> kMemos[] = {{"meTrafik", exe_va::kHelpTraffic}, {"meIsik", exe_va::kHelpLight},
                {"meCop", exe_va::kHelpLitter}, {"mePark", exe_va::kHelpPark}, {"meYaya", exe_va::kHelpPedestrian}};
            for (auto [n, va] : kMemos)
                if (const DfmObject* d = giris_.find(n)) {
                    const auto lines = text_.wrap(exe_->ansi_or(va, ""), F::Medium, static_cast<float>(d->num("Width")));
                    const float lh = 20, h = lh * static_cast<float>(lines.size());
                    float y = o.y + static_cast<float>(d->num("Top")) + (static_cast<float>(d->num("Height")) - h) / 2;
                    for (const auto& l : lines) {
                        text_.draw(l, o.x + static_cast<float>(d->num("Left")), y, F::Medium, {0, 0, 0, 1});
                        y += lh;
                    }
                }
            glDisable(GL_SCISSOR_TEST);
            if (const Texture* t = glyph("SpeedButton7")) r_.quad(470, 647, 65, 65, t);
            break;
        }
        case Screen::DriverList:
        case Screen::NewDriver: {
            const glm::vec2 o = kPanel2;
            r_.quad(o.x, o.y, 289, 273, nullptr, glm::vec4(kPanel, 1));
            if (const Texture* t = image("maps\\zemin01_k.jpg")) r_.quad(o.x + 1, o.y + 1, 287, 271, t, glm::vec4(1), {0, 0, 1, 271.0f / 321.0f});
            if (screen_ == Screen::DriverList) {
                draw_bar(o.x + 8, o.y + 8, 273, 17, exe_->ansi_or(0x517ad8, "Sürücü Listesi"), kPanel, {1, 1, 1}, F::UiBold);
                r_.quad(o.x + 8, o.y + 24, 273, 153, nullptr, glm::vec4(kListBg, 1));
                for (std::size_t i = 0; i < profiles_.size() && i < 7; ++i) {
                    const float y = o.y + 24 + 20 * static_cast<float>(i);
                    const bool sel = static_cast<int>(i) == selected_;
                    if (sel) r_.quad(o.x + 8, y, 273, 20, nullptr, {0, 0, 0.5f, 1});
                    text_.draw(profiles_[i].name() + " " + profiles_[i].surname(), o.x + 11, y + 1, F::Hud, sel ? glm::vec4(1) : glm::vec4(0, 0, 0, 1));
                }
                for (const char* n : {"RxSpeedButton1", "SpeedButton10"})
                    if (const DfmObject* b = giris_.find(n)) draw_button(*b, o);
            } else {
                draw_bar(o.x + 8, o.y + 8, 273, 17, formMsg_, kPanel, {1, 1, 1}, F::UiBold);
                const std::uint32_t labels[4] = {0x517a94, 0x517aa4, 0x517ab8, 0x517ac8};
                const float ly[4] = {41, 65, 89, 114};
                const char* fb[4] = {"Adın ", "Soyadın ", "Yaşın ", "Şehir "};
                for (int i = 0; i < 4; ++i) {
                    r_.quad(o.x + 19, o.y + ly[i], 54, 17, nullptr, glm::vec4(kLabelBlue, 1));
                    text_.draw(exe_->ansi_or(labels[i], fb[i]), o.x + 72, o.y + ly[i] + 1, F::UiBold, {1, 1, 1, 1}, 1);
                    text_.draw("*", o.x + 6, o.y + ly[i], F::Big, {1, 0, 0, 1});
                }
                const float ey[3] = {40, 64, 112};
                for (int i = 0; i < 3; ++i) {
                    r_.quad(o.x + 72, o.y + ey[i], 209, 21, nullptr, {0.5f, 0.5f, 0.5f, 1});
                    r_.quad(o.x + 73, o.y + ey[i] + 1, 207, 19, nullptr, {1, 1, 1, 1});
                    const bool caret = formFocus_ == i && static_cast<long>(clock_ * 2) % 2 == 0;
                    text_.draw(form_[i] + (caret ? "|" : ""), o.x + 75, o.y + ey[i] + 1, F::Hud, {0, 0, 0, 1});
                }
                r_.quad(o.x + 72, o.y + 88, 41, 21, nullptr, {0.5f, 0.5f, 0.5f, 1});
                r_.quad(o.x + 73, o.y + 89, 39, 19, nullptr, {1, 1, 1, 1});
                text_.draw(std::to_string(formAge_) + " ▾", o.x + 75, o.y + 90, F::Ui, {0, 0, 0, 1});
                for (const char* n : {"SpeedButton9", "SpeedButton8"})
                    if (const DfmObject* b = giris_.find(n)) draw_button(*b, o);
            }
            break;
        }
        case Screen::Main:
        case Screen::MapSelect: {
            const glm::vec2 o = panel1();
            r_.quad(o.x, o.y, 705, 481, nullptr, glm::vec4(kPanel, 1));
            for (const char* n : {"SpeedButton3", "SpeedButton4", "SpeedButton6", "SpeedButton5", "SpeedButton13", "SpeedButton12"})
                if (const DfmObject* b = giris_.find(n)) draw_button(*b, o);
            r_.quad(o.x + 48, o.y + 104, 600, 57, nullptr, {0, 0, 0, 1});  // Image5: licence wheels
            if (quiz_)
                for (int i = 0; i < quiz_->correct(); ++i)
                    if (const Texture* t = image("maps\\direksiyon_k.bmp", true)) r_.quad(o.x + 48 - 48 + 50.0f * static_cast<float>(i + 1), o.y + 104, 50, 50, t);
            const glm::vec2 img = o + glm::vec2(49, 169);
            if (const Texture* t = image("maps\\zemin02_k.jpg")) r_.quad(img.x, img.y, 598, 298, t);
            if (quiz_ && quiz_->state() != LicenceQuiz::State::Passed) {
                for (const auto& l : quiz_layout(quiz_->current())) {
                    const auto rc = l.answer >= 0 ? quiz_line_rect(l.answer) : std::nullopt;
                    const bool hover = rc && inside(mouse_, rc->x, rc->y, rc->z, rc->w);
                    const glm::vec4 col = l.answer < 0 ? glm::vec4(1, 0, 0, 1) : hover ? glm::vec4(1, 1, 0, 1) : glm::vec4(0, 0, 0, 1);
                    text_.draw(l.text, img.x + 299, img.y + l.y + 2, F::Medium, {0.5f, 0.5f, 0.5f, 1}, 2);
                    text_.draw(l.text, img.x + 299, img.y + l.y, F::Medium, col, 2);
                }
                if (quiz_->state() == LicenceQuiz::State::Failed) quizFailedShown_ = true;
            }
            if (popupOpen_) {
                const glm::vec2 b = o + glm::vec2(240, 97);
                r_.quad(b.x, b.y, 260, 66, nullptr, {0.4f, 0.4f, 0.4f, 1});
                r_.quad(b.x + 1, b.y + 1, 258, 64, nullptr, {0.94f, 0.94f, 0.94f, 1});
                text_.draw("ToyotaSA İnternet Sitesi... (kapalı)", b.x + 8, b.y + 4, F::Ui, {0.4f, 0.4f, 0.4f, 1});
                text_.draw("Oyunla İlgili Sorularınız İçin... (kapalı)", b.x + 8, b.y + 24, F::Ui, {0.4f, 0.4f, 0.4f, 1});
                text_.draw("Anahtar: " + (profile_ && !profile_->key().empty() ? profile_->key() : std::string("-")), b.x + 8, b.y + 44, F::Ui, {0, 0, 0, 1});
            }
            if (screen_ == Screen::MapSelect) {
                const glm::vec2 m = kMapSel;
                r_.quad(m.x, m.y, 455, 113, nullptr, {0.5f, 0.5f, 0.5f, 1});
                r_.quad(m.x + 1, m.y + 1, 453, 111, nullptr, {1, 1, 1, 1});
                draw_bar(m.x + 2, m.y + 2, 447, 16, giris_.find("Label12") ? giris_.find("Label12")->text("Caption") : "", kPanel, {0, 0, 0}, F::Medium);
                for (int i = 1; i <= 4; ++i)
                    if (const DfmObject* b = giris_.find("btHarita" + std::to_string(i))) draw_button(*b, m, i == 1 || (profile_ && profile_->unlocked() >= i));
            }
            break;
        }
        default: break;
    }
    if (const Texture* t = glyph("SpeedButton2")) r_.quad(975, 718, 41, 41, t);
    if (screen_ == Screen::Loading) {
        // TFrameYukleniyor: black frame, title, map picture, map info.
        const MapData* m = loadingMap_ >= 1 && loadingMap_ <= static_cast<int>(maps_.size()) ? &maps_[static_cast<std::size_t>(loadingMap_ - 1)] : nullptr;
        r_.quad(0, 0, 1024, 768, nullptr, {0, 0, 0, 1});
        const glm::vec2 f{(1024 - 443) / 2.0f, (768 - 277) / 2.0f};
        text_.draw(exe_->ansi_or(exe_va::kLoaderTitle, "Harita Yükleniyor"), 512, f.y + 10, F::Huge, {1, 1, 1, 1}, 2);
        if (m)
            if (const Texture* t = image(m->loadingImage)) r_.quad(f.x, f.y + 57, 443, 150, t);
        static const std::uint32_t kTitles[] = {0x52d300, 0x528ca0, 0x50671c, 0x521014};
        const int s = static_cast<int>(timeLimit_);
        const std::string title = (loadingMap_ >= 1 && loadingMap_ <= 4 ? exe_->ansi_or(kTitles[loadingMap_ - 1], "") : std::string()) +
                                  std::format("{:02}:{:02}:{:02}", s / 3600, s / 60 % 60, s % 60) +
                                  exe_->ansi_or(0x52d340, " - Toplaman gereken puan : ") + std::to_string(required_) + ")";
        float ty = f.y + 215;
        for (const auto& l : text_.wrap(title, F::UiBold, 430)) {
            text_.draw(l, 512, ty, F::UiBold, {1, 1, 1, 1}, 2);
            ty += 16;
        }
    }
    if (!modal_.empty()) {
        const auto lines = text_.wrap(modal_, F::Ui, 460);
        const float h = 80 + 18 * static_cast<float>(lines.size()), x = 512 - 250, y = 384 - h / 2;
        r_.quad(x, y, 500, h, nullptr, {0.3f, 0.3f, 0.3f, 1});
        r_.quad(x + 1, y + 1, 498, 22, nullptr, {0.04f, 0.14f, 0.42f, 1});
        text_.draw(modalCaption_, x + 8, y + 4, F::UiBold, {1, 1, 1, 1});
        r_.quad(x + 1, y + 23, 498, h - 24, nullptr, {0.83f, 0.82f, 0.78f, 1});
        for (std::size_t i = 0; i < lines.size(); ++i) text_.draw(lines[i], x + 20, y + 36 + 18 * static_cast<float>(i), F::Ui, {0, 0, 0, 1});
        r_.quad(512 - 37, y + h - 34, 75, 23, nullptr, {0.4f, 0.4f, 0.4f, 1});
        r_.quad(512 - 36, y + h - 33, 73, 21, nullptr, {0.9f, 0.9f, 0.88f, 1});
        text_.draw("OK", 512, y + h - 31, F::Ui, {0, 0, 0, 1}, 2);
    }
}

}  // namespace tto

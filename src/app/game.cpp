// In-game flow (TFormOyun): map start, HUD, keys, question panel, map end, report.
// Behaviour follows docs/re/RULES.md §3 and docs/re/DRIVING.md §2/§7; HUD canvas is 800x600.
#include <algorithm>
#include <format>

#include "app/app.hpp"
#include "core/log.hpp"

namespace tto {
namespace {
constexpr glm::vec4 kHud{0, 1, 0.498f, 1};
constexpr glm::vec4 kYellow{1, 1, 0, 1};
constexpr glm::vec4 kRapor{0.439f, 0.859f, 0.576f, 1};
constexpr float kLine = 20;

// WrapText(s, 60): break at the last space before column 60.
std::vector<std::string> wrap60(const std::string& s) {
    std::vector<std::string> out;
    std::string rest = s;
    auto cps = [](const std::string& t) {
        std::size_t n = 0;
        for (unsigned char c : t) n += (c & 0xC0) != 0x80;
        return n;
    };
    while (cps(rest) > 60) {
        std::size_t cp = 0, cut = std::string::npos;
        for (std::size_t i = 0; i < rest.size(); ++i) {
            if ((static_cast<unsigned char>(rest[i]) & 0xC0) != 0x80) ++cp;
            if (cp > 60) break;
            if (rest[i] == ' ') cut = i;
        }
        if (cut == std::string::npos) break;
        out.push_back(rest.substr(0, cut));
        rest = rest.substr(cut + 1);
    }
    out.push_back(rest);
    return out;
}
}  // namespace

int App::session_total() const { return perMap_[1] + perMap_[2] + perMap_[3] + perMap_[4]; }

void App::start_map(int index) {
    mapIndex_ = index;
    loadingMap_ = index;
    const MapData& m = maps_.at(static_cast<std::size_t>(index - 1));
    timeLimit_ = cfg_.number(m.timeKey).value_or(m.timeDefault);
    required_ = static_cast<int>(cfg_.number(std::format("Ayarlar-Harita{}Puan", index)).value_or(200));
    goto_screen(Screen::Loading);
    loadingUntil_ = 1.5;  // loader frame stays up while the builder runs
    scene_.reset();
    sim_.reset();
}

void App::load_map(int index) {
    const MapData& m = maps_.at(static_cast<std::size_t>(index - 1));
    scene_ = std::make_unique<Scene>(r_, *fs_, cfg_);
    scene_->load_map(m);
    scene_->load_cars();
    Sim::Config sc;
    sc.seed = static_cast<unsigned>(rng_());
    sim_ = std::make_unique<Sim>(m, scene_->car_params(), scene_->car_colors(rng_), sc);
    scene_->set_camera_mode(1);  // observed first view under Wine is the chase camera ([RT], DRIVING.md §6 check 1)
    answeredSigns_.clear();
    panel_ = {};
    arrowSign_.reset();
    reportVisible_ = false;
    menuMode_ = false;
    started_ = false;
    startDelay_ = 0;
    gameTime_ = 0;
    speechUntil_ = 0;
    pendingFile_.clear();
    showF1_ = showF2_ = false;
    showF3_ = true;
    lastCrash_ = lastHorn_ = 0;
    std::fill(std::begin(keys_), std::end(keys_), false);
    if (auto p = fs_->find("Arabalar\\motor.wav")) engine_ = audio_.loop(audio_.load(*p), 0.0f);
    log_info("map {} '{}' loaded: limit {} s, required {} points", index, m.name, timeLimit_, required_);
}

DriveInput App::drive_input() const {
    DriveInput in;
    const bool frozen = panel_.open || reportVisible_ || gameTime_ < speechUntil_;
    if (frozen) return in;
    in.up = keys_[0];
    in.down = keys_[1];
    in.left = keys_[2];
    in.right = keys_[3];
    in.brake = keys_[4];
    return in;
}

void App::game_update(double dt) {
    if (screen_ == Screen::Loading) return;
    gameTime_ += dt;
    if (!pendingFile_.empty() && gameTime_ >= pendingAt_) {
        speechUntil_ = gameTime_ + speech(pendingDir_, pendingFile_);
        pendingFile_.clear();
    }
    const bool talking = gameTime_ < speechUntil_ || !pendingFile_.empty();
    if (!talking && resumeAfterSpeech_) {
        resumeAfterSpeech_ = false;
        arrowSign_.reset();
        sim_->set_paused(false);
    }
    if (!talking && menuAfterSpeech_) {
        menuAfterSpeech_ = false;
        open_menu_question(false);
    }
    if (!started_) {
        // Startup delay 500 ms, then the greeting speech (0x52dfec).
        startDelay_ += dt;
        if (startDelay_ > 0.5) {
            started_ = true;
            speechUntil_ = gameTime_ + speech("sesler\\giris", "giris001.wav");
        }
    }
    const int crash0 = sim_->crashSounds, horn0 = sim_->hornSounds;
    sim_->update(dt, drive_input());
    if (sim_->crashSounds != crash0)
        if (auto p = fs_->find(std::format("sesler\\crash{}.wav", std::uniform_int_distribution<int>(1, 12)(rng_)))) audio_.play(audio_.load(*p));
    if (sim_->hornSounds != horn0)
        if (auto p = fs_->find("Arabalar\\korna.wav")) audio_.play(audio_.load(*p));
    scene_->update_camera(*sim_, dt);
    if (engine_) {
        // Engine pitch bands (DRIVING.md §8): base 0/20/40 km/h, 300 Hz per km/h over 11500 Hz.
        const float kmh = sim_->player().body->kmh();
        const float base = kmh < 20 ? 0.0f : kmh < 40 ? 20.0f : 40.0f;
        engine_->set_pitch(((kmh - base) * 300 + 11500) / 11025.0f);
        engine_->set_gain(sim_->paused() ? 0.0f : 0.35f);
    }
    if (!sim_->paused() && !menuMode_ && !talking && !reportVisible_ && sim_->time() >= timeLimit_) map_end();
}

void App::game_key(SDL_Keycode k, bool down) {
    const int idx = k == SDLK_UP ? 0 : k == SDLK_DOWN ? 1 : k == SDLK_LEFT ? 2 : k == SDLK_RIGHT ? 3 : k == SDLK_F ? 4 : -1;
    if (idx >= 0) keys_[idx] = down;
    if (!down || screen_ != Screen::Game) return;
    if (reportVisible_) {
        if (k == SDLK_RETURN || k == SDLK_KP_ENTER) close_report();
        return;
    }
    if (gameTime_ < speechUntil_ || panel_.open) return;  // ignored while talking or while a question is open
    switch (k) {
        case SDLK_SPACE:
            if (!menuMode_) {
                sim_->set_paused(!sim_->paused());
                if (!sim_->paused()) arrowSign_.reset();
            }
            break;
        case SDLK_F4:
            if (!sim_->paused() && sim_->rules().score() >= required_) map_end();
            break;
        case SDLK_ESCAPE: open_menu_question(true); break;
        case SDLK_F1: showF1_ = !showF1_; break;
        case SDLK_F2: showF2_ = !showF2_; break;
        case SDLK_F3: showF3_ = !showF3_; break;
        case SDLK_H: sim_->horn(); break;
        case SDLK_C: sim_->toggle_signal(true); break;
        case SDLK_V: sim_->toggle_signal(false); break;
        case SDLK_K: if (!sim_->paused()) scene_->cycle_camera(true); break;
        case SDLK_L: if (!sim_->paused()) scene_->cycle_camera(false); break;
        case SDLK_O: scene_->set_camera_mode(1); break;
        case SDLK_M:
            if (radio_) {
                radio_.reset();
            } else if (!playlist_.empty()) {
                if (auto p = fs_->find(playlist_[static_cast<std::size_t>(radioTrack_) % playlist_.size()])) radio_ = audio_.loop(audio_.load(*p), 0.4f);
            }
            break;
        case SDLK_N:
            if (!playlist_.empty()) {
                radioTrack_ = (radioTrack_ + 1) % static_cast<int>(playlist_.size());
                if (auto p = fs_->find(playlist_[static_cast<std::size_t>(radioTrack_)])) radio_ = audio_.loop(audio_.load(*p), 0.4f);
            }
            break;
        default: break;
    }
}

std::vector<glm::vec4> App::panel_line_rects() const {
    std::vector<glm::vec4> out;
    const auto q = wrap60(panel_.question);
    const float n = static_cast<float>(q.size() + panel_.lines.size());
    float y = 575 - 25 * (n + 3) + kLine * static_cast<float>(q.size());
    for (std::size_t i = 0; i < panel_.lines.size(); ++i, y += kLine) out.push_back({30, y, 560, kLine});
    return out;
}

void App::game_click(glm::vec2 hud, glm::vec2) {
    if (screen_ != Screen::Game || reportVisible_ || gameTime_ < speechUntil_) return;
    if (panel_.open) {
        const auto rects = panel_line_rects();
        for (std::size_t i = 0; i < rects.size(); ++i) {
            const auto& r = rects[i];
            if (hud.x >= r.x && hud.x < r.x + r.z && hud.y >= r.y && hud.y < r.y + r.w && panel_.tags[i] != 0) {
                answer(panel_.tags[i]);
                return;
            }
        }
        return;
    }
    if (!sim_->paused() || menuMode_) return;  // picking only while paused
    const auto pk = scene_->pick(*sim_, hud.x, hud.y, 800, 600);
    if (pk.sign >= 0) open_sign_question(pk.sign);
    else if (pk.car >= 0) open_violator_question(pk.car);
}

void App::open_sign_question(int sign) {
    const auto& s = sim_->map().signs.at(static_cast<std::size_t>(sign));
    const int ne = static_cast<int>(std::count_if(bank_.begin(), bank_.end(), [](const Question& q) { return q.category == "Ehliyet"; }));
    const int id = ne + s.number;  // SoruBank id 71+nn (RULES.md §3.3)
    const auto it = std::find_if(bank_.begin(), bank_.end(), [&](const Question& q) { return q.id == id; });
    if (it == bank_.end()) {
        log_warn("Lehva bigisi bulunamadi : {}", s.number);
        return;
    }
    panel_ = {};
    panel_.open = true;
    panel_.kind = Panel::Sign;
    panel_.sign = sign;
    panel_.group = it->group;
    panel_.questionId = id;
    panel_.question = "Soru : " + it->text + std::format(" (+{})", it->group * 20);
    if (std::find(answeredSigns_.begin(), answeredSigns_.end(), id) != answeredSigns_.end()) {
        panel_.lines = {"", exe_->ansi_or(0x53419c, "Bu soru daha önce soruldu."), ""};
        panel_.tags = {0, 99, 0};
        speechUntil_ = gameTime_ + speech_random("bilmistin", 'b', 5);
        panel_.open = false;
        resumeAfterSpeech_ = true;
        log_info("sign L{} already answered", s.number);
        return;
    }
    const AskedQuestion a = shuffle_question(*it, rng_);
    static const char* pre[3] = {"a ) ", "b ) ", "c ) "};
    for (int i = 0; i < 3; ++i) {
        panel_.lines.push_back(pre[i] + a.line(i));
        panel_.tags.push_back(kPermutations[static_cast<std::size_t>(a.perm)][static_cast<std::size_t>(i)] + 1);
    }
    arrowSign_ = sign;
}

void App::open_violator_question(int car) {
    Car& c = sim_->cars().at(static_cast<std::size_t>(car));
    if (c.violation <= 0) return;  // a clean car does nothing
    const int code = c.violation;
    c.violation = 0;  // each violation can be asked once
    static const std::uint32_t correct[4] = {0, 0x532eec, 0x532f48, 0x532f64};
    static const char* fb[4] = {"", "Bu araç ışık ihlali yaptı.", "Bu araç etrafı kirletiyor.", "Bu araç yanlış park etmiş."};
    const std::string ans[3] = {exe_->ansi_or(correct[code], fb[code]), exe_->ansi_or(0x532f08, "Bu aracın son muayene süresi geçmiş."),
                                exe_->ansi_or(0x532f30, "Aracın farları kırık.")};
    panel_ = {};
    panel_.open = true;
    panel_.kind = Panel::Violator;
    panel_.car = car;
    panel_.violator = code;
    panel_.group = code == 1 ? 5 : 3;
    panel_.question = std::format("Soru :  (+{})", panel_.group * 20);
    const int perm = std::uniform_int_distribution<int>(0, 5)(rng_);
    static const char* pre[3] = {"a ) ", "b ) ", "c ) "};
    for (int i = 0; i < 3; ++i) {
        const int k = kPermutations[static_cast<std::size_t>(perm)][static_cast<std::size_t>(i)];
        panel_.lines.push_back(pre[i] + ans[k]);
        panel_.tags.push_back(k + 1);
    }
}

void App::open_menu_question(bool esc) {
    menuMode_ = true;
    sim_->set_paused(true);
    panel_ = {};
    panel_.open = true;
    panel_.kind = Panel::Menu;
    panel_.menuEsc = esc;
    if (esc) {
        panel_.question = exe_->ansi_or(0x533180, "Oyundan çıkmak istediğinize emin misiniz?");
        panel_.lines = {"", exe_->ansi_or(0x5331ac, " > Hayır, oyuna devam et."), exe_->ansi_or(0x5331c8, " > Evet, oyundan çıkmak istiyorum.")};
        panel_.tags = {0, 2, 3};
    } else {
        panel_.question = exe_->ansi_or(0x5331f4, "Tebrikler ! Devam etmek için seçiniz...");
        const std::string t1 = mapIndex_ < 4 ? exe_->ansi_or(0x533264, " > Bir sonraki haritaya geç.")
                                             : exe_->ansi_or(0x53321c, ">> Sonucu TOYOTASA'ya yolla...");
        panel_.lines = {t1, exe_->ansi_or(0x53323c, " > Bu haritaya devam et."), exe_->ansi_or(0x533258, " > Çıkış.")};
        panel_.tags = {1, 2, 3};
    }
}

void App::answer(int tag) {
    Rules& r = sim_->rules();
    const double now = sim_->wall();
    if (panel_.kind == Panel::Menu) {
        panel_.open = false;
        if (tag == 1) {
            if (mapIndex_ < 4) {
                sim_->reset_clock();
                reportTarget_ = mapIndex_ + 1;
                reportText_ = r.report(session_total());
                reportVisible_ = true;
            } else {
                log_info("game finished: key {} total {}", score_key(session_total()), session_total());
                exit_to_menu();
            }
        } else if (tag == 2) {
            if (sim_->time() >= timeLimit_) sim_->reset_clock();
            menuMode_ = false;
            sim_->set_paused(false);
        } else if (tag == 3) {
            exit_to_menu();
        }
        return;
    }
    if (tag == 99 || tag == 0) return;
    const bool ok = tag == 1;
    panel_.open = false;
    std::string explainDir, explainFile;
    if (panel_.kind == Panel::Sign) {
        r.sign_answer(ok, panel_.group, now);
        if (ok) answeredSigns_.push_back(panel_.questionId);
        explainDir = "sesler\\levha";
        explainFile = std::format("c{}.wav", panel_.questionId - 5);
    } else {
        r.violator_answer(ok, static_cast<Rules::Violator>(panel_.violator), now);
        explainDir = "sesler\\trafik";
        explainFile = std::format("t{:03}.wav", panel_.violator);
    }
    log_info("answer {} -> score {}", ok ? "correct" : "wrong", r.score());
    const double d = ok ? speech_random("dogru", 'd', 30) : speech_random("yanlis", 'y', 30);
    speechUntil_ = gameTime_ + d;
    pendingDir_ = explainDir;
    pendingFile_ = explainFile;
    pendingAt_ = gameTime_ + d;
    resumeAfterSpeech_ = true;
}

void App::map_end() {
    Rules& r = sim_->rules();
    if (r.score() < required_) {
        log_info("map {} failed: {} < {} points", mapIndex_, r.score(), required_);
        r.set_score(0);
        answeredSigns_.clear();
        sim_->reset_clock();
        speechUntil_ = gameTime_ + speech_random("puan_toplayamama", 't', 5);
        reportTarget_ = mapIndex_;
        reportText_ = r.report(session_total());
        reportVisible_ = true;
        sim_->set_paused(true);
        return;
    }
    perMap_[mapIndex_] = r.score();  // overwrite, not max
    log_info("map {} passed with {} points", mapIndex_, r.score());
    static const char* dirs[5] = {"", "ilk_etabi_gecme", "ikinci_etabi_gecme", "", "oyun_bitirme"};
    static const char pre[5] = {0, 'i', 'h', 0, 'f'};
    if (pre[mapIndex_]) speechUntil_ = gameTime_ + speech_random(dirs[mapIndex_], pre[mapIndex_], 5);
    if (profile_ && mapIndex_ < 4 && profile_->unlocked() < mapIndex_ + 1) {
        profile_->set_unlocked(mapIndex_ + 1);
        save_profile();
    }
    menuMode_ = true;
    sim_->set_paused(true);
    if (mapIndex_ == 3) open_menu_question(false);
    else menuAfterSpeech_ = true;
}

void App::close_report() {
    reportVisible_ = false;
    if (reportTarget_ != mapIndex_) {
        start_map(reportTarget_);
        return;
    }
    sim_->rules().reset_attempt();
    sim_->reset_clock();
    menuMode_ = false;
    sim_->set_paused(false);
}

void App::exit_to_menu() {
    save_profile();
    scene_.reset();
    sim_.reset();
    engine_.reset();
    radio_.reset();
    panel_ = {};
    menuMode_ = false;
    goto_screen(Screen::Main);
    play_music("sesler\\trafik_ses2.mp3");
}

void App::signclick(bool correct) {
    // Test helper: click the nearest on-screen sign that has not been answered, then its answer line.
    if (!sim_->paused()) game_key(SDLK_SPACE, true), game_key(SDLK_SPACE, false);
    const glm::mat4 pv = scene_->proj(800, 600) * scene_->view();
    int best = -1;
    float bestD = 1e9f;
    glm::vec2 at{};
    const int ne = static_cast<int>(std::count_if(bank_.begin(), bank_.end(), [](const Question& q) { return q.category == "Ehliyet"; }));
    for (std::size_t i = 0; i < sim_->map().signs.size(); ++i) {
        const auto& s = sim_->map().signs[i];
        if (std::find(answeredSigns_.begin(), answeredSigns_.end(), ne + s.number) != answeredSigns_.end()) continue;
        const glm::vec4 c = pv * glm::vec4(s.pos[0], s.pos[1] + 0.35f, s.pos[2], 1);
        if (c.w <= 0.5f) continue;
        const glm::vec2 n = glm::vec2(c) / c.w;
        if (std::abs(n.x) > 0.95f || std::abs(n.y) > 0.95f) continue;
        if (c.w < bestD) {
            bestD = c.w;
            best = static_cast<int>(i);
            at = {(n.x + 1) * 400, (1 - n.y) * 300};
        }
    }
    if (best < 0) throw std::runtime_error("no unanswered sign on screen");
    game_click(at, at);
    if (!panel_.open) throw std::runtime_error(std::format("click at {:.0f},{:.0f} did not open a question", at.x, at.y));
    const auto rects = panel_line_rects();
    for (std::size_t i = 0; i < panel_.tags.size(); ++i)
        if ((panel_.tags[i] == 1) == correct) {
            const auto& r = rects[i];
            game_click({r.x + 5, r.y + r.w / 2}, {});
            return;
        }
}

void App::game_draw() {
    using F = TextRenderer::Font;
    if (screen_ == Screen::Loading) {
        compute_viewport(1024, 768);
        menu_draw();
        if (screenTime_ > 0.1 && !scene_) {
            try {
                load_map(loadingMap_);
            } catch (const std::exception& e) {
                log_error("map load: {}", e.what());
                message_box(e.what(), "Hata");
                goto_screen(Screen::Main);
                return;
            }
        }
        if (scene_ && screenTime_ >= loadingUntil_) goto_screen(Screen::Game);
        return;
    }
    scene_->draw(*sim_, vw_, vh_, arrowSign_);
    r_.begin_2d(800, 600);
    const Rules& r = sim_->rules();
    if (reportVisible_) {
        if (const Texture* t = image("maps\\Cerceve.jpg")) r_.quad(0, 0, 800, 600, t);
        text_.draw_lines(reportText_, 30, 25, F::Hud, kRapor, 0, 23, true);
    }
    const int el = static_cast<int>(sim_->time());
    text_.draw_shadowed(std::format("Süre : {:02}:{:02}:{:02}", el / 3600, el / 60 % 60, el % 60), 780, 510, F::Hud, kHud, 1);
    text_.draw_shadowed("Puan : " + std::to_string(r.score()), 780, 535, F::Hud, kHud, 1);
    text_.draw_shadowed(std::to_string(static_cast<int>(sim_->player().body->kmh())) + " km/h", 780, 560, F::Hud, kHud, 1);
    text_.draw_shadowed(exe_->ansi_or(0x52dc20, "Soruları yanıtlamak için [BOŞLUK] tuşuna basın..."), 780, 10, F::Hud, kHud, 1);
    if (r.score() >= required_ && mapIndex_ != 4 && std::fmod(gameTime_, 1.2) < 0.7 && !menuMode_)
        text_.draw_shadowed(exe_->ansi_or(0x52dc5c, "Sonraki bölüme geçmek için [F4] tuşuna basın..."), 780, 30, F::Hud, kHud, 1);
    if (showF3_ && !reportVisible_)
        text_.draw_lines(exe_->ansi_or(0x52dbd4, "F1 - Oyun Kontrolleri\nF2 - Bölüm Hakkında Bilgi\nF3 - Gizle/Göster"), 30, 510, F::Hud, kYellow, 0, 20, true);
    if (showF1_ && !reportVisible_) text_.draw_lines(exe_->ansi_or(exe_va::kF1Help, ""), 30, 30, F::Hud, kYellow, 0, 20, true);
    if (showF2_ && !reportVisible_) {
        const int lim = static_cast<int>(timeLimit_);
        std::string t = exe_->ansi_or(0x52f8c0, "Oyun süresi : ") + std::format("{:02}:{:02}:{:02}", lim / 3600, lim / 60 % 60, lim % 60) + "\n" +
                        exe_->ansi_or(0x52f8fc, "Toplaman gereken puan : ") + std::to_string(required_) + "\n\n";
        for (std::uint32_t va : {0x52f920u, 0x52f960u, 0x52f99cu, 0x52f9d4u}) t += exe_->ansi_or(va, "") + "\n";
        t += "\n" + exe_->ansi_or(0x52fa04, "İyi eğlenceler!...");
        text_.draw_lines(t, 30, 30, F::Hud, kYellow, 0, 20, true);
    }
    if (!reportVisible_) {
        float y = 25;
        for (const auto& m : r.messages()) {
            text_.draw_lines(m.text, 30, y, F::Hud, kRapor, 0, 20, true);
            y += 20 * static_cast<float>(1 + std::count(m.text.begin(), m.text.end(), '\n'));
        }
    }
    if (panel_.open) {
        const auto q = wrap60(panel_.question);
        const auto rects = panel_line_rects();
        float y = rects.empty() ? 300 : rects[0].y - kLine * static_cast<float>(q.size());
        r_.quad(20, y - 6, 600, kLine * static_cast<float>(q.size() + panel_.lines.size()) + 12, nullptr, {0, 0, 0, 0.45f});
        for (const auto& l : q) {
            text_.draw_shadowed(l, 30, y, F::Hud, kHud);
            y += kLine;
        }
        for (std::size_t i = 0; i < rects.size(); ++i) {
            const auto& rc = rects[i];
            const bool hover = mouse_.x >= rc.x && mouse_.x < rc.x + rc.z && mouse_.y >= rc.y && mouse_.y < rc.y + rc.w;
            text_.draw_shadowed(panel_.lines[i], 30, rc.y, F::Hud, hover && panel_.tags[i] ? kYellow : kHud);
        }
    }
    if (sim_->paused() && !panel_.open && !reportVisible_ && !menuMode_) text_.draw_shadowed("||", 400, 280, F::Huge, kHud, 2);
}

}  // namespace tto

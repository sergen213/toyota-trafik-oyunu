#include "app/app.hpp"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "core/log.hpp"

namespace tto {

App::App(Options o) : opt_(std::move(o)), rng_(opt_.seed ? opt_.seed : std::random_device{}()) {}

App::~App() {
    stop_movie();
    scene_.reset();
    sim_.reset();
    music_.reset();
    engine_.reset();
    radio_.reset();
    if (r_) {
        text_.shutdown();
        r_->shutdown();
        r_.reset();
    }
    audio_.shutdown();
    if (gl_) SDL_GL_DestroyContext(gl_);
    if (joy_) SDL_CloseJoystick(joy_);
    if (win_) SDL_DestroyWindow(win_);
    SDL_Quit();
}

bool App::init() {
    fs_ = std::make_unique<GameFs>(opt_.gameDir);
    try {
        cfg_ = parse_vdd(read_text(fs_->need("HG.vdd")));
        bank_ = parse_sorubank(read_file(fs_->need("Data\\SoruBank.dat")));
        maps_ = load_maps(opt_.dataDir / "maps.json");
        exe_ = std::make_unique<ExeStrings>(fs_->need("TrafikOyunu.exe"));
        const auto res = pe_rcdata(read_file(fs_->need("TrafikOyunu.exe")));
        giris_ = parse_dfm(res.at("TGIRISFORM"));
    } catch (const std::exception& e) {
        log_error("game data: {}", e.what());
        return false;
    }
    if (!exe_->valid()) log_warn("TrafikOyunu.exe is not the v2 build; some texts fall back to built-in copies");

    // Profiles: the native game keeps its own UserData.dat; the original install is never written.
    std::filesystem::path save = opt_.saveDir;
    if (save.empty()) {
        const char* x = std::getenv("XDG_DATA_HOME");
        const char* h = std::getenv("HOME");
        save = x && *x ? std::filesystem::path(x) / "toyota-trafik" : std::filesystem::path(h ? h : ".") / ".local/share/toyota-trafik";
    }
    std::filesystem::create_directories(save);
    userDataPath_ = save / "UserData.dat";
    try {
        if (std::filesystem::exists(userDataPath_)) profiles_ = parse_profiles(read_file(userDataPath_));
        else if (auto orig = fs_->find("Data\\UserData.dat")) profiles_ = parse_profiles(read_file(*orig));
    } catch (const std::exception& e) {
        log_warn("profiles ignored: {}", e.what());
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_JOYSTICK)) {
        log_error("SDL_Init: {}", SDL_GetError());
        return false;
    }
    SDL_WindowFlags base = SDL_WINDOW_RESIZABLE;
    if (opt_.fullscreen) base |= SDL_WINDOW_FULLSCREEN;
    const char* title = "Cancan ile Toyota Trafik Oyunu";
    // Vulkan first (ray-query shadows when available), OpenGL 3.3 as the fallback.
    if (opt_.renderer != "gl") {
        win_ = SDL_CreateWindow(title, opt_.width, opt_.height, base | SDL_WINDOW_VULKAN);
        std::string err;
        if (win_) r_ = make_vk_renderer(win_, !opt_.fixedStep, &err);
        if (!r_) {
            log_warn("Vulkan renderer unavailable: {}", err.empty() ? SDL_GetError() : err);
            if (win_) SDL_DestroyWindow(win_);
            win_ = nullptr;
            if (opt_.renderer == "vulkan") return false;
        }
    }
    if (!r_) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);  // v2 requests aa2xHQ
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 2);
        win_ = SDL_CreateWindow(title, opt_.width, opt_.height, base | SDL_WINDOW_OPENGL);
        if (!win_) {
            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
            win_ = SDL_CreateWindow(title, opt_.width, opt_.height, base | SDL_WINDOW_OPENGL);
        }
        if (!win_ || !(gl_ = SDL_GL_CreateContext(win_))) {
            log_error("OpenGL 3.3 window: {}", SDL_GetError());
            return false;
        }
        SDL_GL_SetSwapInterval(opt_.fixedStep ? 0 : 1);
        r_ = make_gl_renderer();
        if (!r_) return false;
    }
    if (r_->ray_tracing_supported()) r_->set_ray_tracing(opt_.rayTracing);
    if (opt_.frameGen && opt_.fsr < 0) opt_.fsr = 1;
    if (r_->upscaler_supported()) r_->set_upscaler(opt_.fsr);
    else if (opt_.fsr >= 0) log_warn("FSR 3.1.4 needs the Vulkan renderer on a capable GPU; upscaling stays off");
    if (opt_.frameGen) {
        if (r_->frame_generation_supported()) r_->set_frame_generation(true);
        else log_warn("FSR 3.1.4 frame generation unsupported here");
    }
    if (!text_.init(*r_)) return false;
    static const char* fsrNames[] = {"native AA", "quality", "balanced", "performance", "ultra performance"};
    log_info("renderer {} on {}{}{}{}", r_->name(), r_->device(), r_->ray_tracing() ? " (ray-traced shadows)" : "",
             r_->upscaler() >= 0 ? std::string(", FSR 3.1.4 ") + fsrNames[r_->upscaler()] : std::string(),
             r_->frame_generation() ? " + frame generation" : "");
    if (!opt_.mute) audio_.init();
    SDL_StartTextInput(win_);

    playlist_load();
    menu_load();
    if (!opt_.script.empty()) load_script();
    return true;
}

double App::speech(const std::string& dir, const std::string& file) {
    auto p = fs_->find(dir + "\\" + file);
    if (!p) return 0;
    const Audio::Clip* c = audio_.load(*p);
    if (!c) {
        // No audio device: estimate WAV length from the file size so timing stays the same.
        // No audio device: take the length from the WAV header so timing stays the same.
        try {
            const auto d = read_file(*p);
            if (d.size() < 44) return 0;
            const std::uint32_t rate = d[28] | d[29] << 8 | d[30] << 16 | static_cast<std::uint32_t>(d[31]) << 24;
            return rate ? static_cast<double>(d.size() - 44) / rate : 0;
        } catch (...) {
            return 0;
        }
    }
    audio_.play(c);
    log_info("speech {}\\{}", dir, file);
    const double bps = static_cast<double>(c->spec.freq) * c->spec.channels * SDL_AUDIO_BYTESIZE(c->spec.format);
    return bps > 0 ? static_cast<double>(c->pcm.size()) / bps : 0;
}

double App::speech_random(const std::string& dir, char prefix, int count) {
    const int n = std::uniform_int_distribution<int>(1, count)(rng_);
    return speech("sesler\\" + dir, std::format("{}{:03}.wav", prefix, n));
}

void App::play_music(const std::string& winPath) {
    if (auto p = fs_->find(winPath)) music_ = audio_.loop(audio_.load(*p), 0.6f);
}

bool App::start_movie(const std::string& winPath, bool withAudio) {
    stop_movie();
    const auto p = fs_->find(winPath);
    if (!p || !movie_.video.open(*p)) return false;
    Image black;
    black.w = movie_.video.width();
    black.h = movie_.video.height();
    black.rgba.assign(static_cast<std::size_t>(black.w) * black.h * 4, 0);
    movie_.tex = r_->create_texture(black, false);
    movie_.t = 0;
    movie_.active = true;
    if (withAudio && audio_.enabled()) {
        const auto pcm = movie_.video.audio_s16(44100);
        if (!pcm.empty()) movie_.audio = audio_.start(audio_.from_pcm(p->string() + "#audio", 44100, 2, pcm), 1.0f);
    }
    log_info("video {} ({}x{}, {:.1f} s)", winPath, black.w, black.h, movie_.video.duration());
    movie_tick(0);
    return true;
}

void App::movie_tick(double dt) {
    if (!movie_.active) return;
    movie_.t += dt;
    if (movie_.video.frame_at(movie_.t, movie_.img)) r_->update_texture(movie_.tex, movie_.img);
}

void App::stop_movie() {
    movie_.audio.reset();
    if (movie_.tex.id) r_->destroy_texture(movie_.tex);
    movie_.video.close();
    movie_.active = false;
}

void App::draw_movie(float x, float y, float w, float h) {
    if (movie_.active && movie_.tex.id) r_->quad(x, y, w, h, &movie_.tex);
}

void App::compute_viewport(int lw, int lh) {
    lw_ = lw;
    lh_ = lh;
    const float s = std::min(static_cast<float>(w_) / static_cast<float>(lw), static_cast<float>(h_) / static_cast<float>(lh));
    vw_ = std::max(1, static_cast<int>(static_cast<float>(lw) * s));
    vh_ = std::max(1, static_cast<int>(static_cast<float>(lh) * s));
    vx_ = (w_ - vw_) / 2;
    vy_ = (h_ - vh_) / 2;
    r_->set_viewport(vx_, vy_, vw_, vh_);
}

glm::vec2 App::to_logical(float x, float y) const {
    // SDL reports window coordinates; the GL drawable may be scaled (HiDPI).
    int ww = 1, wh = 1;
    SDL_GetWindowSize(win_, &ww, &wh);
    const float px = x * static_cast<float>(w_) / static_cast<float>(ww), py = y * static_cast<float>(h_) / static_cast<float>(wh);
    // GL origin is bottom-left; vy_ counts from the bottom, symmetric letterbox so it is the same from the top.
    return {(px - static_cast<float>(vx_)) * static_cast<float>(lw_) / static_cast<float>(vw_),
            (py - static_cast<float>(vy_)) * static_cast<float>(lh_) / static_cast<float>(vh_)};
}

void App::handle(const SDL_Event& e) {
    switch (e.type) {
        case SDL_EVENT_QUIT: quit_ = true; break;
        case SDL_EVENT_MOUSE_MOTION: mouse_ = to_logical(e.motion.x, e.motion.y); break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            mouse_ = to_logical(e.button.x, e.button.y);
            if (screen_ == Screen::Game) game_click(mouse_, mouse_);
            else menu_click(mouse_, e.button.button == SDL_BUTTON_RIGHT);
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL:
            if (screen_ != Screen::Game) menu_wheel(e.wheel.y);
            break;
        case SDL_EVENT_KEY_DOWN:
            if (e.key.key == SDLK_F11) {
                opt_.fullscreen = !opt_.fullscreen;
                SDL_SetWindowFullscreen(win_, opt_.fullscreen);
            } else if (e.key.key == SDLK_F10 && r_->upscaler_supported()) {  // native extra: cycle FSR off/native/quality/../ultra
                const int q = r_->upscaler() >= 4 ? -1 : r_->upscaler() + 1;
                r_->set_upscaler(q);
                static const char* names[] = {"FSR kapalı", "FSR doğal AA", "FSR kalite", "FSR dengeli", "FSR performans", "FSR ultra performans"};
                log_info("{}", names[q + 1]);
                if (sim_) sim_->rules().notice(names[q + 1], sim_->wall());
            } else if (e.key.key == SDLK_F8 && r_->frame_generation_supported()) {  // native extra: FSR frame generation
                opt_.frameGen = !opt_.frameGen;
                if (opt_.frameGen && r_->upscaler() < 0) r_->set_upscaler(1);
                r_->set_frame_generation(opt_.frameGen);
                const char* t = r_->frame_generation() ? "Kare üretimi açık" : "Kare üretimi kapalı";
                log_info("{}", t);
                if (sim_) sim_->rules().notice(t, sim_->wall());
            } else if (e.key.key == SDLK_F9 && r_->ray_tracing_supported()) {  // native extra: ray-traced shadows on/off
                r_->set_ray_tracing(!r_->ray_tracing());
                log_info("ray-traced shadows {}", r_->ray_tracing() ? "on" : "off");
                if (sim_) sim_->rules().notice(r_->ray_tracing() ? "Işın izlemeli gölgeler açık" : "Işın izlemeli gölgeler kapalı", sim_->wall());
            } else if (screen_ == Screen::Game) {
                if (!e.key.repeat) game_key(e.key.key, true);
            } else {
                menu_key(e.key.key);
            }
            break;
        case SDL_EVENT_KEY_UP:
            if (screen_ == Screen::Game) game_key(e.key.key, false);
            break;
        case SDL_EVENT_JOYSTICK_ADDED:
            if (!joy_) {
                joy_ = SDL_OpenJoystick(e.jdevice.which);
                if (joy_) log_info("joystick: {}", SDL_GetJoystickName(joy_));
            }
            break;
        case SDL_EVENT_JOYSTICK_REMOVED:
            if (joy_ && SDL_GetJoystickID(joy_) == e.jdevice.which) {
                SDL_CloseJoystick(joy_);
                joy_ = nullptr;
            }
            break;
        case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
            // OnJoystickButtonChange (0x530cc0) feeds the KeyDown handler: button 2 toggles pause.
            if (screen_ == Screen::Game && joyMode_ && e.jbutton.button == 1) game_key(SDLK_SPACE, true), game_key(SDLK_SPACE, false);
            break;
        case SDL_EVENT_TEXT_INPUT:
            if (screen_ != Screen::Game) menu_text(e.text.text);
            break;
        default: break;
    }
}

void App::frame(double dt) {
    screenTime_ += dt;
    audio_.update();
    movie_tick(dt);
    if (movie_.active && screen_ != Screen::Game && screen_ != Screen::Loading && movie_.t > movie_.video.duration() + 0.1) stop_movie();
    if (screen_ == Screen::Game) game_update(dt);
    SDL_GetWindowSizeInPixels(win_, &w_, &h_);
    r_->begin_window(w_, h_);
    if (screen_ == Screen::Game || screen_ == Screen::Loading) {
        compute_viewport(800, 600);
        game_draw();
    } else {
        compute_viewport(1024, 768);
        menu_draw();
    }
}

int App::run() {
    if (!init()) return 1;
    std::uint64_t last = SDL_GetTicksNS();
    while (!quit_) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) handle(e);
        const std::uint64_t now = SDL_GetTicksNS();
        double dt = opt_.fixedStep ? 1.0 / 60.0 : static_cast<double>(now - last) * 1e-9;
        last = now;
        dt = std::min(dt, 0.1);
        clock_ += dt;
        if (!steps_.empty()) run_script(dt);
        frame(dt);
        r_->end_window();
        if (gl_) SDL_GL_SwapWindow(win_);
    }
    return exitCode_;
}

// ---------------------------------------------------------------- script
// One command per line (# comments). Times are simulated seconds.
//   wait <s> | click <x> <y> | key <name> | down <name> | up <name> | hold <name> <s> | type <text>
//   shot <name> | quiz (answer the current licence question correctly) | quizwrong
//   until <condition> <timeout>  (screen=<Title|Main|...|Game>, report, panel, menuq, score>=N)
//   expect <condition> | log <text> | quit

void App::load_script() {
    std::istringstream in(read_text(opt_.script));
    int n = 0;
    for (std::string l; std::getline(in, l);) {
        ++n;
        if (auto h = l.find('#'); h != std::string::npos) l.resize(h);
        std::istringstream ls(l);
        Step s;
        s.line = n;
        if (!(ls >> s.cmd)) continue;
        if (s.cmd == "type" || s.cmd == "log") {
            std::string rest;
            std::getline(ls, rest);
            s.args.push_back(rest.empty() ? rest : rest.substr(1));
        } else {
            for (std::string a; ls >> a;) s.args.push_back(a);
        }
        steps_.push_back(std::move(s));
    }
    if (!opt_.shotDir.empty()) std::filesystem::create_directories(opt_.shotDir);
}

namespace {
SDL_Keycode key_by_name(const std::string& n) {
    if (n == "space") return SDLK_SPACE;
    if (n == "enter") return SDLK_RETURN;
    if (n == "esc") return SDLK_ESCAPE;
    if (n == "up") return SDLK_UP;
    if (n == "down") return SDLK_DOWN;
    if (n == "left") return SDLK_LEFT;
    if (n == "right") return SDLK_RIGHT;
    if (n.size() >= 2 && n[0] == 'F') return SDLK_F1 + static_cast<SDL_Keycode>(std::stoi(n.substr(1)) - 1);
    if (n.size() == 1) return static_cast<SDL_Keycode>(n[0]);
    return SDLK_UNKNOWN;
}
}  // namespace

void App::run_script(double) {
    auto cond = [&](const std::string& c) -> bool {
        static const char* names[] = {"Title", "Help", "DriverList", "NewDriver", "Main", "MapSelect", "Loading", "Game", "Playlist"};
        if (c.starts_with("screen=")) return c.substr(7) == names[static_cast<int>(screen_)];
        if (c == "report") return reportVisible_;
        if (c == "panel") return panel_.open;
        if (c == "menuq") return panel_.open && panel_.kind == Panel::Menu;
        if (c == "quizpassed") return licenceThisRun_;
        if (c.starts_with("score>=")) return sim_ && sim_->rules().score() >= std::stoi(c.substr(7));
        if (c.starts_with("unlocked>=")) return profile_ && profile_->unlocked() >= std::stoi(c.substr(10));
        if (c.starts_with("map=")) return mapIndex_ == std::stoi(c.substr(4));
        if (c == "started") return started_;
        if (c == "movie") return movie_.active;
        if (c == "cutscene") return cutscene_;
        if (c == "nocutscene") return !cutscene_;
        if (c.starts_with("playlist=")) return static_cast<int>(playlist_.size()) == std::stoi(c.substr(9));
        if (c == "paused") return sim_ && sim_->paused();
        if (c == "idle") return started_ && gameTime_ >= speechUntil_ && pendingFile_.empty();
        return false;
    };
    while (stepIdx_ < steps_.size()) {
        if (clock_ < waitUntil_) return;
        if (!waitFor_.empty()) {
            if (cond(waitFor_)) {
                waitFor_.clear();
            } else if (clock_ > waitDeadline_) {
                log_error("script: timed out waiting for {}", waitFor_);
                exitCode_ = 3;
                quit_ = true;
                return;
            } else {
                return;
            }
        }
        const Step& s = steps_[stepIdx_++];
        auto arg = [&](std::size_t i) -> const std::string& {
            static const std::string empty;
            return i < s.args.size() ? s.args[i] : empty;
        };
        auto num = [&](std::size_t i) { return std::stod(arg(i)); };
        try {
            if (s.cmd == "wait") {
                waitUntil_ = clock_ + num(0);
            } else if (s.cmd == "click") {
                mouse_ = {static_cast<float>(num(0)), static_cast<float>(num(1))};
                if (screen_ == Screen::Game) game_click(mouse_, mouse_);
                else menu_click(mouse_, false);
                waitUntil_ = clock_ + 0.1;
            } else if (s.cmd == "key" || s.cmd == "down" || s.cmd == "up") {
                const SDL_Keycode k = key_by_name(arg(0));
                if (screen_ == Screen::Game) {
                    if (s.cmd != "up") game_key(k, true);
                    if (s.cmd != "down") game_key(k, false);
                } else if (s.cmd != "up") {
                    menu_key(k);
                }
                waitUntil_ = clock_ + 0.05;
            } else if (s.cmd == "hold") {
                game_key(key_by_name(arg(0)), true);
                Step up{"up", {arg(0)}, s.line};
                Step w{"wait", {arg(1)}, s.line};
                steps_.insert(steps_.begin() + static_cast<std::ptrdiff_t>(stepIdx_), {w, up});
            } else if (s.cmd == "type") {
                menu_text(arg(0).c_str());
            } else if (s.cmd == "shot") {
                frame(0);
                screenshot(arg(0));
            } else if (s.cmd == "quiz" || s.cmd == "quizwrong") {
                if (!quiz_ || quiz_->state() != LicenceQuiz::State::Asking) throw std::runtime_error("no quiz question");
                const int c = quiz_->current().correct_line();
                const auto rc = quiz_line_rect(s.cmd == "quiz" ? c : (c + 1) % 3);
                if (!rc) throw std::runtime_error("quiz line not laid out");
                menu_click({rc->x + rc->z / 2, rc->y + rc->w / 2}, false);
                waitUntil_ = clock_ + 0.2;
            } else if (s.cmd == "sign" || s.cmd == "signwrong") {
                try {
                    signclick(s.cmd == "sign");
                } catch (const std::exception& e) {
                    log_warn("script line {}: {}", s.line, e.what());  // not fatal: the next attempt drives further
                    if (sim_ && sim_->paused() && !panel_.open) sim_->set_paused(false);
                }
                waitUntil_ = clock_ + 0.2;
            } else if (s.cmd == "answer") {
                const int tag = std::stoi(arg(0));
                const auto rects = panel_line_rects();
                bool done = false;
                for (std::size_t i = 0; i < panel_.tags.size() && !done; ++i)
                    if (panel_.tags[i] == tag) {
                        game_click({rects[i].x + 5, rects[i].y + rects[i].w / 2}, {});
                        done = true;
                    }
                if (!done) throw std::runtime_error("no panel line with tag " + arg(0));
                waitUntil_ = clock_ + 0.2;
            } else if (s.cmd == "loadmap") {  // test-only: like the original's Demo keys 1..5
                licenceThisRun_ = true;
                start_map(std::stoi(arg(0)));
            } else if (s.cmd == "peds") {
                int n = 0;
                float best = 1e9f;
                for (const auto& p : sim_->pedestrians())
                    if (p.active) {
                        ++n;
                        best = std::min(best, glm::length(p.pos - sim_->player().body->position()));
                    }
                log_info("pedestrians active {} nearest {:.1f} m", n, best);
                if (arg(0) == "shot")
                    for (const auto& p : sim_->pedestrians())
                        if (p.active && glm::length(p.pos - sim_->player().body->position()) <= best + 0.01f) {
                            scene_->look_at(p.pos - p.dir * 4.0f + glm::vec3(1.5f, 1.8f, 0), p.pos + glm::vec3(0, 0.9f, 0));
                            scene_->draw(*sim_, vw_, vh_, std::nullopt);  // draw with this view, then capture
                            screenshot(arg(1));
                        }
            } else if (s.cmd == "cars") {  // test: log active AI cars; "cars shot <name>" views the nearest from above
                std::string list;
                int near = -1;
                float best = 1e9f;
                const glm::vec3 P = sim_->player().body->position();
                for (std::size_t i = 0; i < sim_->cars().size(); ++i) {
                    const Car& c = sim_->cars()[i];
                    if (!c.active || c.player) continue;
                    const float d = glm::length(c.body->position() - P);
                    list += std::format(" {}{}{}{}@{:.0f}m", kVehicleTypes[c.type - 1].key, c.siren ? "(siren)" : "", c.parked ? "(parked)" : "",
                                        c.unloading ? "(unloading)" : "", d);
                    if (d < best && (arg(2).empty() || std::stoi(arg(2)) == c.type)) best = d, near = static_cast<int>(i);
                }
                log_info("cars:{}", list);
                if (arg(0) == "shot" && near >= 0) {
                    const Car& c = sim_->cars()[static_cast<std::size_t>(near)];
                    const glm::vec3 cp = c.body->position();
                    scene_->look_at(cp - c.body->forward() * 7.0f + glm::vec3(2.5f, 3.0f, 0), cp);
                    scene_->draw(*sim_, vw_, vh_, std::nullopt);
                    screenshot(arg(1));
                }
            } else if (s.cmd == "crossing") {  // test: view the first level crossing and the train
                if (!sim_->crossings().empty()) {
                    const auto& x = sim_->crossings()[0];
                    scene_->look_at(x.pos + x.facing * 9.0f + glm::vec3(-3, 3, 0), x.pos);
                    scene_->draw(*sim_, vw_, vh_, std::nullopt);
                    screenshot(arg(0));
                    log_info("crossing closed={} boom={:.0f} train running={} z={:.0f}", x.closed, x.boom, sim_->train().running, sim_->train().z);
                }
            } else if (s.cmd == "pldir") {  // test: point the music-list editor at a folder of the game install
                if (auto d = fs_->find(arg(0))) {
                    pl_.dir = *d;
                    pl_.scan();
                }
            } else if (s.cmd == "label") {
            } else if (s.cmd == "skipif") {
                if (cond(arg(0))) {
                    while (stepIdx_ < steps_.size() && !(steps_[stepIdx_].cmd == "label" && steps_[stepIdx_].args.size() && steps_[stepIdx_].args[0] == arg(1))) ++stepIdx_;
                    if (stepIdx_ == steps_.size()) throw std::runtime_error("label not found: " + arg(1));
                }
            } else if (s.cmd == "autodrive") {
                autodriveUntil_ = gameTime_ + num(0);
                waitUntil_ = clock_ + num(0);
            } else if (s.cmd == "until") {
                waitFor_ = arg(0);
                waitDeadline_ = clock_ + (s.args.size() > 1 ? num(1) : 30.0);
                return;
            } else if (s.cmd == "expect") {
                if (!cond(arg(0))) {
                    log_error("script line {}: expectation failed: {}", s.line, arg(0));
                    exitCode_ = 3;
                    quit_ = true;
                    return;
                }
                log_info("script: ok {}", arg(0));
            } else if (s.cmd == "log") {
                log_info("script: {}", arg(0));
            } else if (s.cmd == "quit") {
                quit_ = true;
                return;
            } else {
                throw std::runtime_error("unknown command " + s.cmd);
            }
        } catch (const std::exception& e) {
            log_error("script line {}: {}", s.line, e.what());
            exitCode_ = 3;
            quit_ = true;
            return;
        }
    }
}

void App::screenshot(const std::string& name) {
    if (opt_.shotDir.empty()) return;
    Image full = r_->read_pixels(w_, h_);
    Image img;
    img.w = vw_;
    img.h = vh_;
    img.rgba.resize(static_cast<std::size_t>(vw_) * vh_ * 4);
    const int top = h_ - vy_ - vh_;
    for (int y = 0; y < vh_; ++y)
        std::copy_n(full.rgba.data() + (static_cast<std::size_t>(top + y) * w_ + vx_) * 4, static_cast<std::size_t>(vw_) * 4,
                    img.rgba.data() + static_cast<std::size_t>(y) * vw_ * 4);
    const auto p = opt_.shotDir / (name + ".png");
    save_png(p, img);
    log_info("screenshot {}", p.string());
}

}  // namespace tto

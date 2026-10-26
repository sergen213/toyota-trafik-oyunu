#pragma once
#include <SDL3/SDL.h>

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "app/exe_strings.hpp"
#include "app/scene.hpp"
#include "core/fs.hpp"
#include "data/mapdata.hpp"
#include "data/sorubank.hpp"
#include "data/userdata.hpp"
#include "data/vdd.hpp"
#include "formats/dfm.hpp"
#include "game/quiz.hpp"
#include "game/traffic.hpp"
#include "platform/audio.hpp"
#include "render/gl.hpp"
#include "render/text.hpp"

namespace tto {

struct Options {
    std::filesystem::path gameDir;  // original install (contains TrafikOyunu.exe)
    std::filesystem::path dataDir;  // this project's data/ (maps.json)
    std::filesystem::path saveDir;  // where UserData.dat is kept (default: XDG data home)
    std::filesystem::path script;   // automated input script (testing), see docs/TESTING.md
    std::filesystem::path shotDir;  // screenshots written here by the script
    bool fixedStep = false;         // simulate 1/60 s per frame regardless of real time (tests)
    bool mute = false;
    bool fullscreen = false;
    int width = 1024, height = 768;
    unsigned seed = 0;  // 0 = random
};

class App {
public:
    explicit App(Options o);
    ~App();
    int run();  // 0 = normal quit, 1 = init failure, 3 = script expectation failed

    enum class Screen { Title, Help, DriverList, NewDriver, Main, MapSelect, Loading, Game };

private:
    // ---- lifecycle (app.cpp)
    bool init();
    void frame(double dt);
    void handle(const SDL_Event& e);
    void present();
    double speech(const std::string& dir, const std::string& file);  // returns duration (s)
    double speech_random(const std::string& dir, char prefix, int count);
    void play_music(const std::string& winPath);

    // ---- menu (menu.cpp); logical canvas 1024x768
    void menu_load();
    void menu_draw();
    void menu_click(glm::vec2 p, bool right);
    void menu_key(SDL_Keycode k);
    void menu_text(const char* utf8);
    void menu_wheel(float dy);
    void goto_screen(Screen s);
    void start_quiz();
    void quiz_click(int line);
    std::optional<glm::vec4> quiz_line_rect(int line) const;  // logical rect of answer line 0..2
    void try_start_map(int map);
    const Texture* glyph(const std::string& obj, int index = 0, int count = 1);
    const Texture* image(const std::string& winPath, bool key = false);
    void draw_button(const DfmObject& b, glm::vec2 origin, bool enabled = true);
    bool hit_button(const DfmObject& b, glm::vec2 origin, glm::vec2 p) const;
    void draw_bar(float x, float y, float w, float h, const std::string& text, glm::vec3 bg, glm::vec3 fg, TextRenderer::Font f);
    void message_box(std::string text, std::string caption);
    glm::vec2 panel1() const { return {160, 313}; }

    // ---- game (game.cpp); HUD canvas 800x600
    void start_map(int index);
    void load_map(int index);  // builder + placement
    void game_update(double dt);
    void game_draw();
    void game_key(SDL_Keycode k, bool down);
    void game_click(glm::vec2 hud, glm::vec2 px);
    void map_end();
    void open_sign_question(int sign);
    void open_violator_question(int car);
    void open_menu_question(bool esc);
    void answer(int tag);
    void close_report();
    void exit_to_menu();
    int session_total() const;
    std::vector<glm::vec4> panel_line_rects() const;  // HUD rects of the question panel lines
    DriveInput drive_input() const;

    // ---- script (app.cpp)
    void load_script();
    void run_script(double dt);
    void screenshot(const std::string& name);

    // ---- coordinates
    void compute_viewport(int lw, int lh);
    glm::vec2 to_logical(float x, float y) const;

    Options opt_;
    SDL_Window* win_ = nullptr;
    SDL_GLContext gl_ = nullptr;
    int w_ = 1024, h_ = 768;
    int vx_ = 0, vy_ = 0, vw_ = 1024, vh_ = 768, lw_ = 1024, lh_ = 768;
    bool quit_ = false;
    int exitCode_ = 0;
    std::mt19937 rng_;

    std::unique_ptr<GameFs> fs_;
    VddNode cfg_;
    std::vector<Question> bank_;
    std::vector<MapData> maps_;
    std::unique_ptr<ExeStrings> exe_;
    DfmObject giris_;  // TGirisForm layout from the user's executable
    Renderer r_;
    TextRenderer text_;
    Audio audio_;
    std::map<std::string, const Texture*> glyphs_;

    // ---- profiles
    std::filesystem::path userDataPath_;
    std::vector<Profile> profiles_;
    std::optional<Profile> profile_;
    int needCorrect_ = 8;
    bool licenceThisRun_ = false;
    int perMap_[5] = {};
    void save_profile();

    // ---- menu state
    Screen screen_ = Screen::Title;
    double screenTime_ = 0, introTime_ = -1;
    int selected_ = -1;
    std::string form_[3];  // name, surname, city
    int formAge_ = 12, formFocus_ = 0;
    std::string formMsg_;
    std::optional<LicenceQuiz> quiz_;
    bool quizFailedShown_ = false;
    std::string modal_, modalCaption_;
    float helpScroll_ = 0;
    bool popupOpen_ = false;
    glm::vec2 mouse_{0};
    std::unique_ptr<Audio::Voice> music_;
    struct DecoCar {
        int img = 0;
        float x = 0;
    };
    std::vector<DecoCar> deco_;
    double nextDeco_ = 0;

    // ---- game state
    int mapIndex_ = 0;
    std::unique_ptr<Scene> scene_;
    std::unique_ptr<Sim> sim_;
    double loadingUntil_ = 0;
    int loadingMap_ = 0;
    bool started_ = false;
    double startDelay_ = 0;
    double timeLimit_ = 240;
    int required_ = 150;
    bool showF1_ = false, showF2_ = false, showF3_ = true;
    bool reportVisible_ = false;
    std::string reportText_;
    int reportTarget_ = 0;
    struct Panel {
        bool open = false;
        enum Kind { Sign, Violator, Menu } kind = Sign;
        std::string question;
        std::vector<std::string> lines;
        std::vector<int> tags;  // 1 correct, 2/3 wrong, 99 none; Menu: 1..3
        int group = 1, violator = 0, sign = -1, car = -1, questionId = 0;
        bool menuEsc = false;
    } panel_;
    std::vector<int> answeredSigns_;  // SoruBank ids answered correctly this map
    std::optional<int> arrowSign_;
    double speechUntil_ = 0;
    std::string pendingDir_, pendingFile_;  // explanation clip queued after the dogru/yanlis speech
    double pendingAt_ = 0;
    double gameTime_ = 0;  // wall clock while in Screen::Game
    void signclick(bool correct);
    bool resumeAfterSpeech_ = false, menuAfterSpeech_ = false;
    std::unique_ptr<Audio::Voice> engine_, radio_;
    int radioTrack_ = 0;
    std::vector<std::string> playlist_;
    int lastCrash_ = 0, lastHorn_ = 0;
    bool menuMode_ = false;
    bool keys_[8] = {};  // up, down, left, right, f, space

    // ---- script
    struct Step {
        std::string cmd;
        std::vector<std::string> args;
        int line = 0;
    };
    std::vector<Step> steps_;
    std::size_t stepIdx_ = 0;
    double waitUntil_ = 0, clock_ = 0;
    std::string waitFor_;
    double waitDeadline_ = 0;
};

}  // namespace tto

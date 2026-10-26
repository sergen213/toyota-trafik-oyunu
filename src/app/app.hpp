#pragma once
#include <SDL3/SDL.h>

#include <filesystem>
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
#include "game/quiz.hpp"
#include "game/traffic.hpp"
#include "platform/audio.hpp"
#include "render/gl.hpp"
#include "render/text.hpp"

namespace tto {

struct Options {
    std::filesystem::path gameDir;   // original install (contains TrafikOyunu.exe)
    std::filesystem::path dataDir;   // this project's data/ (maps.json)
    std::filesystem::path saveDir;   // where UserData.dat is kept (default: XDG data home)
    int startMap = 0;                // >0: skip menus and start this map (testing)
    std::string script;              // automated input script (testing), see docs/TESTING.md
    std::filesystem::path shotDir;   // screenshots written here by the script
    bool hidden = false;             // no visible window (offscreen smoke tests)
    bool mute = false;
    int width = 1024, height = 768;
    unsigned seed = 0;               // 0 = random
    bool fullscreen = false;
};

class App {
public:
    explicit App(Options o);
    ~App();
    int run();

    // UI helpers shared by the menu (logical 1024x768 canvas).
    enum class Screen { Title, Help, DriverList, NewDriver, Main, Quiz, MapSelect, Loading, Game };

private:
    friend class Menu;
    // Lifecycle
    bool init();
    void frame(double dt);
    void handle(const SDL_Event& e);
    void draw_menu();
    void draw_game();
    void play_speech(const std::string& dir, const std::string& prefix, int count);
    // Game flow
    void start_map(int index);
    void unload_map();
    void map_end();
    void open_question_for_sign(int sign);
    void open_question_for_car(int car);
    void answer_question(int line);
    void open_menu_question(bool esc);
    void save_profile();
    int session_total() const;
    // Script
    void run_script(double dt);
    void screenshot(const std::string& name);
    // Coordinates
    glm::vec2 to_logical(float x, float y, int lw, int lh) const;

    Options opt_;
    SDL_Window* win_ = nullptr;
    SDL_GLContext gl_ = nullptr;
    int w_ = 1024, h_ = 768;
    bool quit_ = false;
    std::mt19937 rng_;

    std::unique_ptr<GameFs> fs_;
    VddNode cfg_;
    std::vector<Question> bank_;
    std::vector<MapData> maps_;
    std::unique_ptr<ExeStrings> exe_;
    Renderer r_;
    TextRenderer text_;
    Audio audio_;

    // Profiles
    std::filesystem::path userDataPath_;
    std::vector<Profile> profiles_;
    std::optional<Profile> profile_;
    int needCorrect_ = 8;
    bool licenceThisRun_ = false;
    int perMap_[5] = {};

    // Menu state
    Screen screen_ = Screen::Title;
    double screenTime_ = 0;
    int selected_ = -1;
    std::string form_[3];  // name, surname, city
    int formAge_ = 12, formFocus_ = 0;
    std::string formMsg_ = "Lütfen formu doldurun...";
    std::optional<LicenceQuiz> quiz_;
    std::vector<int> quizWheels_;
    std::string modal_;
    float helpScroll_ = 0;
    glm::vec2 mouse_{0};
    std::unique_ptr<Audio::Voice> music_;

    // Game state
    int mapIndex_ = 0;
    std::unique_ptr<Scene> scene_;
    std::unique_ptr<Sim> sim_;
    double loadingUntil_ = 0;
    bool started_ = false;
    double timeLimit_ = 240;
    int required_ = 150;
    bool showF1_ = false, showF2_ = false, showF3_ = true;
    bool reportVisible_ = false;
    int reportTarget_ = 0;
    struct Panel {
        bool open = false;
        enum Kind { Sign, Violator, Menu } kind = Sign;
        std::string question;
        std::vector<std::string> lines;
        std::vector<int> tags;  // 1 correct, 2/3 wrong, 99 none; Menu: 1..3
        int group = 1, violator = 0, signNumber = 0, sign = -1;
        bool menuEsc = false;
    } panel_;
    std::vector<int> answeredSigns_;
    std::optional<int> arrowSign_;
    double speechUntil_ = 0;
    bool resumeAfterSpeech_ = false, menuAfterSpeech_ = false;
    std::unique_ptr<Audio::Voice> engine_;
    int lastCrash_ = 0, lastHorn_ = 0;
    bool inMenuMode_ = false;

    // Script
    struct Step {
        double at;
        std::string cmd, arg;
    };
    std::vector<Step> steps_;
    std::size_t stepIdx_ = 0;
    double scriptTime_ = 0;
    DriveInput scriptInput_;
    bool scriptActive_ = false;
};

}  // namespace tto

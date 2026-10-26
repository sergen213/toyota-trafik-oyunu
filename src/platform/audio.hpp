#pragma once
#include <SDL3/SDL.h>

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace tto {

// Minimal mixer on SDL3 audio streams: one-shot effects, looping voices with
// pitch (engine), and an MP3 music playlist decoded with libmpg123.
class Audio {
public:
    struct Clip {
        SDL_AudioSpec spec{};
        std::vector<std::uint8_t> pcm;
    };
    class Voice {
    public:
        ~Voice();
        void set_gain(float g);
        void set_pitch(float ratio);  // 1.0 = original speed
        void stop();
        bool playing() const;

    private:
        friend class Audio;
        SDL_AudioStream* s = nullptr;
        const Clip* clip = nullptr;
        bool loop = false;
    };

    bool init();
    void shutdown();
    bool enabled() const { return dev_ != 0; }
    const Clip* load(const std::filesystem::path& p);  // WAV or MP3, cached
    void play(const Clip* c, float gain = 1.0f);
    std::unique_ptr<Voice> loop(const Clip* c, float gain = 1.0f);
    void update();  // refill looping voices; call once per frame
    void set_master(float g);

private:
    SDL_AudioDeviceID dev_ = 0;
    std::unordered_map<std::string, Clip> clips_;
    std::vector<SDL_AudioStream*> oneshots_;
    std::vector<Voice*> loops_;
    friend class Voice;
};

}  // namespace tto

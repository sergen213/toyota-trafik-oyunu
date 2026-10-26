#include "platform/audio.hpp"

#include <mpg123.h>

#include <algorithm>

#include "core/fs.hpp"
#include "core/log.hpp"

namespace tto {
namespace {

Audio* g_audio = nullptr;

bool decode_mp3(const std::filesystem::path& p, Audio::Clip& c) {
    static bool inited = (mpg123_init(), true);
    (void)inited;
    int err = 0;
    mpg123_handle* h = mpg123_new(nullptr, &err);
    if (!h) return false;
    mpg123_format_none(h);
    for (long rate : {8000L, 11025L, 16000L, 22050L, 24000L, 32000L, 44100L, 48000L})
        mpg123_format(h, rate, MPG123_STEREO | MPG123_MONO, MPG123_ENC_SIGNED_16);
    bool ok = false;
    if (mpg123_open(h, p.string().c_str()) == MPG123_OK) {
        long rate = 0;
        int ch = 0, enc = 0;
        if (mpg123_getformat(h, &rate, &ch, &enc) == MPG123_OK) {
            c.spec = {SDL_AUDIO_S16LE, ch, static_cast<int>(rate)};
            std::uint8_t buf[16384];
            std::size_t got = 0;
            int r;
            while ((r = mpg123_read(h, buf, sizeof buf, &got)) == MPG123_OK || r == MPG123_NEW_FORMAT)
                c.pcm.insert(c.pcm.end(), buf, buf + got);
            c.pcm.insert(c.pcm.end(), buf, buf + got);
            ok = !c.pcm.empty();
        }
        mpg123_close(h);
    }
    mpg123_delete(h);
    return ok;
}

}  // namespace

bool Audio::init() {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        log_warn("audio disabled: {}", SDL_GetError());
        return false;
    }
    dev_ = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (!dev_) {
        log_warn("audio device: {}", SDL_GetError());
        return false;
    }
    g_audio = this;
    return true;
}

void Audio::shutdown() {
    for (auto* s : oneshots_) SDL_DestroyAudioStream(s);
    oneshots_.clear();
    for (auto* v : loops_) {
        SDL_DestroyAudioStream(v->s);
        v->s = nullptr;
    }
    loops_.clear();
    if (dev_) SDL_CloseAudioDevice(dev_);
    dev_ = 0;
    g_audio = nullptr;
}

const Audio::Clip* Audio::load(const std::filesystem::path& p) {
    const std::string key = p.string();
    if (auto it = clips_.find(key); it != clips_.end()) return it->second.pcm.empty() ? nullptr : &it->second;
    Clip& c = clips_[key];
    if (to_lower_ascii(p.extension().string()) == ".mp3") {
        if (!decode_mp3(p, c)) log_warn("mp3 decode failed: {}", key);
    } else {
        Uint8* buf = nullptr;
        Uint32 len = 0;
        if (SDL_LoadWAV(key.c_str(), &c.spec, &buf, &len)) {
            c.pcm.assign(buf, buf + len);
            SDL_free(buf);
        } else {
            log_warn("wav load failed: {}: {}", key, SDL_GetError());
        }
    }
    return c.pcm.empty() ? nullptr : &c;
}

void Audio::play(const Clip* c, float gain) {
    if (!dev_ || !c) return;
    oneshots_.erase(std::remove_if(oneshots_.begin(), oneshots_.end(),
                                   [](SDL_AudioStream* s) {
                                       if (SDL_GetAudioStreamQueued(s) > 0) return false;
                                       SDL_DestroyAudioStream(s);
                                       return true;
                                   }),
                    oneshots_.end());
    SDL_AudioStream* s = SDL_CreateAudioStream(&c->spec, nullptr);
    if (!s) return;
    SDL_SetAudioStreamGain(s, gain);
    SDL_PutAudioStreamData(s, c->pcm.data(), static_cast<int>(c->pcm.size()));
    SDL_FlushAudioStream(s);
    SDL_BindAudioStream(dev_, s);
    oneshots_.push_back(s);
}

const Audio::Clip* Audio::from_pcm(const std::string& key, int rate, int channels, const std::vector<std::int16_t>& s16) {
    Clip& c = clips_[key];
    c.spec = {SDL_AUDIO_S16, channels, rate};
    c.pcm.assign(reinterpret_cast<const std::uint8_t*>(s16.data()), reinterpret_cast<const std::uint8_t*>(s16.data() + s16.size()));
    return c.pcm.empty() ? nullptr : &c;
}

std::unique_ptr<Audio::Voice> Audio::start(const Clip* c, float gain) {
    auto v = loop(c, gain);
    v->loop = false;
    if (v->s) SDL_FlushAudioStream(v->s);
    return v;
}

std::unique_ptr<Audio::Voice> Audio::loop(const Clip* c, float gain) {
    auto v = std::make_unique<Voice>();
    if (!dev_ || !c) return v;
    v->s = SDL_CreateAudioStream(&c->spec, nullptr);
    v->clip = c;
    v->loop = true;
    SDL_SetAudioStreamGain(v->s, gain);
    SDL_PutAudioStreamData(v->s, c->pcm.data(), static_cast<int>(c->pcm.size()));
    SDL_BindAudioStream(dev_, v->s);
    loops_.push_back(v.get());
    return v;
}

void Audio::update() {
    for (auto* v : loops_)
        if (v->s && v->loop && SDL_GetAudioStreamQueued(v->s) < static_cast<int>(v->clip->pcm.size()))
            SDL_PutAudioStreamData(v->s, v->clip->pcm.data(), static_cast<int>(v->clip->pcm.size()));
}

void Audio::set_master(float g) {
    if (dev_) SDL_SetAudioDeviceGain(dev_, g);
}

Audio::Voice::~Voice() {
    if (g_audio) std::erase(g_audio->loops_, this);
    if (s) SDL_DestroyAudioStream(s);
}

void Audio::Voice::set_gain(float g) {
    if (s) SDL_SetAudioStreamGain(s, g);
}

void Audio::Voice::set_pitch(float r) {
    if (s) SDL_SetAudioStreamFrequencyRatio(s, std::clamp(r, 0.05f, 8.0f));
}

void Audio::Voice::stop() {
    if (s) {
        SDL_DestroyAudioStream(s);
        s = nullptr;
    }
}

bool Audio::Voice::playing() const { return s != nullptr && (loop || SDL_GetAudioStreamQueued(s) > 0); }

}  // namespace tto

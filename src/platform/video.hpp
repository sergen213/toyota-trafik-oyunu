#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

#include "render/image.hpp"

namespace tto {

// FFmpeg-based player for the original clips: MPEG-1 car cutscenes (Video\*.mpg) and the
// Indeo 4.1 intro (Video\cancan.avi, PCM audio). Frames are decoded on demand by time.
class Video {
public:
    Video();
    ~Video();
    Video(const Video&) = delete;
    Video& operator=(const Video&) = delete;

    bool open(const std::filesystem::path& p);  // false on any error (missing codec, bad file)
    void close();
    bool is_open() const;
    int width() const;
    int height() const;
    double duration() const;
    // Decode up to time t (seconds from the start of the stream); returns true when `out`
    // received a new frame. Seeking backwards restarts the stream.
    bool frame_at(double t, Image& out);
    // Whole audio track as interleaved S16 stereo at `rate` Hz (empty without audio).
    std::vector<std::int16_t> audio_s16(int rate);

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

}  // namespace tto

#include "platform/video.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <algorithm>

#include "core/log.hpp"

namespace tto {

struct Video::Impl {
    AVFormatContext* fmt = nullptr;
    AVCodecContext* vdec = nullptr;
    SwsContext* sws = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* pkt = nullptr;
    int vstream = -1, astream = -1;
    double lastPts = -1, tb = 0, start = 0;
    bool eof = false;
    std::filesystem::path path;

    void close() {
        sws_freeContext(sws);
        sws = nullptr;
        avcodec_free_context(&vdec);
        av_frame_free(&frame);
        av_packet_free(&pkt);
        avformat_close_input(&fmt);
        vstream = astream = -1;
        lastPts = -1;
        eof = false;
    }

    bool open(const std::filesystem::path& p) {
        close();
        path = p;
        if (avformat_open_input(&fmt, p.string().c_str(), nullptr, nullptr) < 0) return false;
        if (avformat_find_stream_info(fmt, nullptr) < 0) return false;
        vstream = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        astream = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (vstream < 0) return false;
        AVStream* st = fmt->streams[vstream];
        const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
        if (!codec) return false;
        vdec = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(vdec, st->codecpar);
        if (avcodec_open2(vdec, codec, nullptr) < 0) return false;
        frame = av_frame_alloc();
        pkt = av_packet_alloc();
        tb = av_q2d(st->time_base);
        start = st->start_time != AV_NOPTS_VALUE ? static_cast<double>(st->start_time) * tb : 0;
        return true;
    }

    // Next decoded video frame, false at the end.
    bool next() {
        while (true) {
            const int r = avcodec_receive_frame(vdec, frame);
            if (r == 0) return true;
            if (r != AVERROR(EAGAIN) || eof) return false;
            if (av_read_frame(fmt, pkt) < 0) {
                eof = true;
                avcodec_send_packet(vdec, nullptr);
                continue;
            }
            if (pkt->stream_index == vstream) avcodec_send_packet(vdec, pkt);
            av_packet_unref(pkt);
        }
    }

    void convert(Image& out) {
        sws = sws_getCachedContext(sws, frame->width, frame->height, static_cast<AVPixelFormat>(frame->format), frame->width, frame->height,
                                   AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
        out.w = frame->width;
        out.h = frame->height;
        out.rgba.resize(static_cast<std::size_t>(out.w) * out.h * 4);
        std::uint8_t* dst[4] = {out.rgba.data(), nullptr, nullptr, nullptr};
        int stride[4] = {out.w * 4, 0, 0, 0};
        sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dst, stride);
    }
};

Video::Video() : d_(std::make_unique<Impl>()) {}
Video::~Video() { d_->close(); }

bool Video::open(const std::filesystem::path& p) {
    if (d_->open(p)) return true;
    log_warn("video {}: cannot open/decode", p.string());
    d_->close();
    return false;
}

void Video::close() { d_->close(); }
bool Video::is_open() const { return d_->vdec != nullptr; }
int Video::width() const { return d_->vdec ? d_->vdec->width : 0; }
int Video::height() const { return d_->vdec ? d_->vdec->height : 0; }
double Video::duration() const { return d_->fmt && d_->fmt->duration > 0 ? static_cast<double>(d_->fmt->duration) / AV_TIME_BASE : 0; }

bool Video::frame_at(double t, Image& out) {
    if (!is_open()) return false;
    if (t + 0.02 < d_->lastPts) {  // rewind
        const auto p = d_->path;
        if (!d_->open(p)) return false;
    }
    bool got = false;
    while (d_->lastPts < t) {
        if (!d_->next()) break;
        const std::int64_t ts = d_->frame->best_effort_timestamp;
        d_->lastPts = ts == AV_NOPTS_VALUE ? d_->lastPts + 0.04 : static_cast<double>(ts) * d_->tb - d_->start;
        got = true;
    }
    if (got) d_->convert(out);
    return got;
}

std::vector<std::int16_t> Video::audio_s16(int rate) {
    std::vector<std::int16_t> pcm;
    if (!is_open() || d_->astream < 0) return pcm;
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, d_->path.string().c_str(), nullptr, nullptr) < 0) return pcm;
    avformat_find_stream_info(fmt, nullptr);
    AVStream* st = fmt->streams[d_->astream];
    const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
    AVCodecContext* dec = codec ? avcodec_alloc_context3(codec) : nullptr;
    SwrContext* swr = nullptr;
    if (dec && avcodec_parameters_to_context(dec, st->codecpar) >= 0 && avcodec_open2(dec, codec, nullptr) >= 0) {
        AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        swr_alloc_set_opts2(&swr, &stereo, AV_SAMPLE_FMT_S16, rate, &dec->ch_layout, dec->sample_fmt, dec->sample_rate, 0, nullptr);
        swr_init(swr);
        AVPacket* pkt = av_packet_alloc();
        AVFrame* fr = av_frame_alloc();
        auto drain = [&] {
            while (avcodec_receive_frame(dec, fr) == 0) {
                const int outN = swr_get_out_samples(swr, fr->nb_samples);
                const std::size_t old = pcm.size();
                pcm.resize(old + static_cast<std::size_t>(outN) * 2);
                std::uint8_t* out = reinterpret_cast<std::uint8_t*>(pcm.data() + old);
                const int n = swr_convert(swr, &out, outN, const_cast<const std::uint8_t**>(fr->extended_data), fr->nb_samples);
                pcm.resize(old + static_cast<std::size_t>(std::max(n, 0)) * 2);
            }
        };
        while (av_read_frame(fmt, pkt) >= 0) {
            if (pkt->stream_index == d_->astream && avcodec_send_packet(dec, pkt) >= 0) drain();
            av_packet_unref(pkt);
        }
        avcodec_send_packet(dec, nullptr);
        drain();
        av_frame_free(&fr);
        av_packet_free(&pkt);
    }
    swr_free(&swr);
    avcodec_free_context(&dec);
    avformat_close_input(&fmt);
    return pcm;
}

}  // namespace tto

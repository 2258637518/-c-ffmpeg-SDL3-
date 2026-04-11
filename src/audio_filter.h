#pragma once

extern "C" {
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
}

class AudioFilter{
public:
    AudioFilter();
    ~AudioFilter();
    bool init(AVCodecContext *codec_ctx, AVRational time_base);
    int add_frame(AVFrame* frame);
    int get_frame(AVFrame* frame);
    AVRational get_out_time_base();
    void set_speed(double speed);
    void flush(AVCodecContext* codec_ctx, AVRational time_base);

private:
    AVFilterGraph* graph = nullptr;
    AVFilterContext* src_ctx = nullptr;
    AVFilterContext* sink_ctx = nullptr;
    double current_speed = 1.0;
};
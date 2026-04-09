#pragma once
#include "thread_queue.h"
#include "media_clock.h"
#include <cstdint>
#include <string>
#include <atomic>
extern "C"{
#include <libavformat/avformat.h>
#include <libavcodec/packet.h>
#include <libavutil/rational.h>
#include <libavutil/frame.h>
}

enum class PlayState{
    PLAYING,
    PAUSE,
    STOPPED
};

struct MediaState
{
    std::string filename;
    PlayState state;

    AVFormatContext *fmt_ctx;
    ThreadQueue<AVPacket*>video_pkt_queue;
    ThreadQueue<AVPacket*>audio_pkt_queue;
    ThreadQueue<AVFrame*>video_frame_queue;
    ThreadQueue<AVFrame*>audio_frame_queue;

    int video_stream_idx=-1;
    int audio_stream_idx=-1;

    MediaClock video_clock;
    MediaClock audio_clock;
    AVRational video_time_base;
    AVRational audio_time_base;

    std::atomic<bool>seek_req;
    std::atomic<int64_t> seek_pos;
    std::atomic<bool>audio_need_clear;

    std::atomic<int> global_seek_serial{0};
    std::atomic<int> video_decoder_serial{0};
    std::atomic<int> audio_decoder_serial{0};

    MediaState(const std::string &filename);
    ~MediaState();
};
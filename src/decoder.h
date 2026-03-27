#pragma once
#include "media_state.h"
#include <thread>
extern "C" {
#include <libavcodec/avcodec.h>
}

class Decoder {
public:
    Decoder(MediaState *state, int stream_idx);
    virtual ~Decoder();

    bool open();
    void start();
    void stop();

protected:
    virtual void decode_thread_loop() = 0;

    MediaState *state;
    int stream_idx;
    AVCodecContext *codec_ctx;
    std::thread decode_thread;
    bool running;
};
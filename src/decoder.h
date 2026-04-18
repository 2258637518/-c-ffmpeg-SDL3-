#pragma once
#include "media_state.h"
#include <libavcodec/codec.h>
#include <thread>
extern "C" {
#include <libavcodec/avcodec.h>
}

//作为基类包含视频和音频相似的的函数和变量
class Decoder {
public:
    Decoder(MediaState *state, int stream_idx);
    virtual ~Decoder();

    bool open();
    void start();
    void stop();

protected:
    virtual void decode_thread_loop() = 0;//设置为子类必须实现的虚函数
    virtual void pre_codec_open(const AVCodec* codec){}//检查是否支持gpu硬解码
    MediaState *state;
    int stream_idx;
    AVCodecContext *codec_ctx;
    std::thread decode_thread;
    bool running;
};
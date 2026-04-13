#pragma once
#include "decoder.h"
extern "C" {
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}
class VideoDecoder : public Decoder {
public:
    VideoDecoder(MediaState *state, int stream_idx);
    ~VideoDecoder();
    bool open();
    enum AVPixelFormat hw_pix_fmt = AV_PIX_FMT_NONE;
protected:
    void decode_thread_loop() override;
    void pre_codec_open(const AVCodec* codec) override;
    SwsContext *sws_ctx;
    AVPixelFormat out_pix_fmt=AV_PIX_FMT_YUV420P;
    int out_width;
    int out_height;
};

#include "decoder.h"
#include "media_state.h"
#include <thread>
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavcodec/codec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libavcodec/packet.h>
}

Decoder::Decoder(MediaState *state, int stream_idx)
{
    this->state = state;
    this->stream_idx = stream_idx;
    this->codec_ctx = nullptr;
    this->running = false;
}

Decoder::~Decoder()
{
    if (this->running)
    {
        this->stop();
    }
    avcodec_free_context(&this->codec_ctx);
}

bool Decoder::open()
{
    AVCodecParameters *codec_par = state->fmt_ctx->streams[stream_idx]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(codec_par->codec_id);
    if (!codec)
    {
        return false;
    }

    
    this->codec_ctx = avcodec_alloc_context3(codec);
    if (avcodec_parameters_to_context(codec_ctx, codec_par) < 0)
    {
        avcodec_free_context(&this->codec_ctx);
        return false;
    }
    pre_codec_open(codec);
    if (avcodec_open2(codec_ctx, codec, NULL) < 0)
    {
        avcodec_free_context(&this->codec_ctx);
        return false;
    }
    return true;
}

void Decoder::start()
{
    this->running = true;
    decode_thread = std::thread(&Decoder::decode_thread_loop, this);
}

void Decoder::stop()
{
    this->running = false;
    if (stream_idx == state->video_stream_idx)
    {
        state->video_pkt_queue.abort();
    }
    else if (stream_idx == state->audio_stream_idx)
    {
        state->audio_pkt_queue.abort();
    }
    if (decode_thread.joinable())
    {
        decode_thread.join();
    }
}
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
    const AVCodec *codec = nullptr;
    
    if (codec_par->codec_id == AV_CODEC_ID_AV1)
    {
        codec = avcodec_find_decoder_by_name("av1");
    } 
    
    if (!codec) 
    {
        codec = avcodec_find_decoder(codec_par->codec_id);//根据编码格式寻找解码程序
    }
    if (!codec)
    {
        return false;
    }

    
    this->codec_ctx = avcodec_alloc_context3(codec);//为解码器创建上下文
    if (avcodec_parameters_to_context(codec_ctx, codec_par) < 0)//从解封装器（Demuxer）获取到的流参数（AVCodecParameters），拷贝到解码器上下文
    {
        avcodec_free_context(&this->codec_ctx);
        return false;
    }
    pre_codec_open(codec);
    if (avcodec_open2(codec_ctx, codec, NULL) < 0)//正式启动解码器
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
#include "audio_decoder.h"
#include <chrono>
#include <thread>
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavcodec/packet.h>
}

AudioDecoder::AudioDecoder(MediaState *state, int stream_idx)
    : Decoder(state, stream_idx)
{
}

AudioDecoder::~AudioDecoder()
{
    if (swr_ctx)
    {
        swr_free(&swr_ctx);
    }
}

void AudioDecoder::decode_thread_loop()
{
    AVFrame *frame = av_frame_alloc();
    AVPacket *pkt = nullptr;
    while (running)
    {
        while (state->audio_frame_queue.size() >= 75 && running)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        pkt = state->audio_pkt_queue.pop();
        if (!pkt) break;
        if (avcodec_send_packet(codec_ctx, pkt) == 0)
        {
            while (avcodec_receive_frame(codec_ctx, frame) >= 0)
            {
                // 1. 分配重采样后的输出帧
                AVFrame *out_frame = av_frame_alloc();
                out_frame->format = AV_SAMPLE_FMT_FLT;
                av_channel_layout_default(&out_frame->ch_layout, 2);
                out_frame->sample_rate = 44100;
                out_frame->pts = frame->pts;
                
                // 2. 计算输出样本数并分配内存缓冲区
                int out_nb_samples = swr_get_out_samples(swr_ctx, frame->nb_samples);
                out_frame->nb_samples = out_nb_samples;
                av_frame_get_buffer(out_frame, 0);

                // 3. 执行重采样转换
                int ret = swr_convert(swr_ctx,
                                      out_frame->data, out_frame->nb_samples,
                                      (const uint8_t **)frame->data, frame->nb_samples);
                
                if (ret > 0)
                {
                    // 修正实际输出的样本数
                    out_frame->nb_samples = ret;
                    state->audio_frame_queue.push(out_frame);
                }
                else
                {
                    av_frame_free(&out_frame);
                }
                // 每解码并处理完一帧后取消引用，以便复用 frame 对象
                av_frame_unref(frame);
            }
        }
        av_packet_free(&pkt);
    }
    if (frame) av_frame_free(&frame);
}

bool AudioDecoder::open()
{
    // 调用基类通用的解码器初始化逻辑
    if (!Decoder::open()) {
        return false;
    }

    // SDL 的 audio_stream 给定的 format 为 SDL_AUDIO_F32 (交错 Float 32 位)
    // 并且是双声道、44100 采样率
    AVChannelLayout out_ch_layout;
    av_channel_layout_default(&out_ch_layout, 2);

    int ret = swr_alloc_set_opts2(
        &swr_ctx,
        &out_ch_layout,
        AV_SAMPLE_FMT_FLT,     // 交错版本 float
        44100,
        &codec_ctx->ch_layout,
        codec_ctx->sample_fmt,
        codec_ctx->sample_rate,
        0,
        nullptr
    );

    if (ret < 0) {
        return false;
    }

    return swr_init(swr_ctx) >= 0;
}

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
    : Decoder(state, stream_idx), swr_ctx(nullptr)
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
        while (state->audio_frame_queue.size() >= 15 && running)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        pkt = state->audio_pkt_queue.pop();
        if (!pkt) break;

        //如果当前pkt时哨兵包,则执行队列冲刷
        if(pkt->data && pkt->size==5&&std::string((char*)pkt->data,5)=="FLUSH")
        {
            avcodec_flush_buffers(codec_ctx);//音频数据分块,buffer也要冲刷
            state->audio_frame_queue.flush([](AVFrame* &f){
                av_frame_free(&f);
            });
            filter.flush(codec_ctx, state->audio_time_base);
            state->audio_decoder_serial = state->global_seek_serial.load();//音频序列跟进全局序列
            current_source_sec = -1.0;
            av_packet_free(&pkt);
            continue;
        }
        if (avcodec_send_packet(codec_ctx, pkt) == 0)
        {
            while (avcodec_receive_frame(codec_ctx, frame) >= 0)
            {
                if (current_source_sec < 0 && frame->pts != AV_NOPTS_VALUE) {
                    current_source_sec = frame->pts * av_q2d(state->audio_time_base);
                }
                filter.set_speed(state->player_speed.load());
                if (filter.add_frame(frame) >= 0)
                {
                    AVFrame *filter_frame = av_frame_alloc();
                    // 循环从滤镜中获取所有处理好的帧
                    while (filter.get_frame(filter_frame) >= 0)
                    {
                        resample_and_push(filter_frame);
                        // 滤镜帧交给下游后，解除当前数据块引用，以便接下一帧数据
                        av_frame_unref(filter_frame);
                    }
                    av_frame_free(&filter_frame);
                }
                // 原始解码出的 frame 送给滤镜后，解除引用
                av_frame_unref(frame);
            }
        }
        av_packet_free(&pkt);
    }
    if (frame) av_frame_free(&frame);
}

void AudioDecoder::resample_and_push(AVFrame *in_frame)
{
    // 1. 分配重采样后的输出帧
    AVFrame *out_frame = av_frame_alloc();
    out_frame->format = AV_SAMPLE_FMT_FLT;
    av_channel_layout_default(&out_frame->ch_layout, 2);
    out_frame->sample_rate = 44100;

    // 基于实际消耗的源时长完美投影正确的 PTS
    if (current_source_sec >= 0) {
        // 输出帧被挂载上理论上绝对正确的源时间刻度
        out_frame->pts = current_source_sec / av_q2d(state->audio_time_base);
        // 本次输出帧所代表的原始时长 = (输出样本数 / 采样率) * 当前倍速
        double source_duration = (in_frame->nb_samples / (double)in_frame->sample_rate) * state->player_speed.load();
        // 累积到下一次
        current_source_sec += source_duration;
    } else {
        out_frame->pts = av_rescale_q(in_frame->pts, filter.get_out_time_base(), state->audio_time_base);
    }
    
    // 2. 计算输出样本数并分配内存缓冲区
    int out_nb_samples = swr_get_out_samples(swr_ctx, in_frame->nb_samples);
    out_frame->nb_samples = out_nb_samples;
    av_frame_get_buffer(out_frame, 0);

    // 3. 执行重采样转换
    int ret = swr_convert(swr_ctx,
                          out_frame->data, out_frame->nb_samples,
                          (const uint8_t **)in_frame->extended_data, in_frame->nb_samples);
    
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

    filter.init(codec_ctx, state->audio_time_base);

    return swr_init(swr_ctx) >= 0;
}

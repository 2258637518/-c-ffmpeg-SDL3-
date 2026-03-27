#include "video_decoder.h"
#include "decoder.h"
#include <chrono>
#include <thread>
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavcodec/packet.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

VideoDecoder::VideoDecoder(MediaState *state, int stream_idx)
    : Decoder(state, stream_idx)
{
    sws_ctx = nullptr;
}

VideoDecoder::~VideoDecoder()
{
    if (sws_ctx)
    {
        sws_freeContext(sws_ctx);
    }
}

void VideoDecoder::decode_thread_loop()
{
    AVFrame *frame = av_frame_alloc();
    AVPacket *pkt = nullptr;
    while (running)
    {
        while (state->video_frame_queue.size() >= 75 && running)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        pkt = state->video_pkt_queue.pop();
        if (!pkt) break;
        if (avcodec_send_packet(codec_ctx, pkt) == 0)
        {
            while (avcodec_receive_frame(codec_ctx, frame) >= 0)
            {
                if (frame->width <= 0 || frame->height <= 0) {
                    av_frame_unref(frame);
                    continue; 
                }
                sws_ctx = sws_getCachedContext(sws_ctx, frame->width, frame->height, (AVPixelFormat)frame->format, out_width, out_height, out_pix_fmt, SWS_BILINEAR, nullptr, nullptr, nullptr);
                
                AVFrame *out_frame = av_frame_alloc();
                out_frame->format = out_pix_fmt;
                out_frame->width = out_width;
                out_frame->height = out_height;
                out_frame->pts = frame->pts;
                av_frame_get_buffer(out_frame, 0);
                
                int ret = sws_scale(sws_ctx, (const uint8_t* const*)frame->data, frame->linesize, 0, frame->height, out_frame->data, out_frame->linesize);
                if (ret > 0)
                {
                    state->video_frame_queue.push(out_frame);
                }
                else
                {
                    av_frame_free(&out_frame);
                }
                
                // 每解码完成一帧并压入队列后，清空 frame 给下一次使用，防止大面积内存泄漏
                av_frame_unref(frame);
            }
        }
        av_packet_free(&pkt);
    }
    if (frame) av_frame_free(&frame);
}

bool VideoDecoder::open()
{
    if(!Decoder::open())
    {
        return false;
    }
    
    // 初始化目标视频宽高为解码器原始宽高
    this->out_width = codec_ctx->width;
    this->out_height = codec_ctx->height;
    
    return true;
}

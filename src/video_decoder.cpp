#include "video_decoder.h"
#include "decoder.h"
#include <chrono>
#include <libavcodec/codec.h>
#include <libavutil/buffer.h>
#include <thread>
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavcodec/packet.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
#include <libavutil/hwcontext.h>
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
    AVFrame *pre_frame = av_frame_alloc();
    AVPacket *pkt = nullptr;
    while (running)
    {
        while (state->video_frame_queue.size() >= 75 && running)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        pkt = state->video_pkt_queue.pop();
        if (!pkt) break;
        //处理哨兵包
        if(pkt->data && pkt->size==5&&std::string((char*)pkt->data,5)=="FLUSH")
        {
            avcodec_flush_buffers(codec_ctx);
            state->video_frame_queue.flush([](AVFrame* &f){
                av_frame_free(&f);
            });
            state->video_decoder_serial = state->global_seek_serial.load(); 
            av_packet_free(&pkt);
            continue;
        }
        if (avcodec_send_packet(codec_ctx, pkt) == 0)
        {
            while (avcodec_receive_frame(codec_ctx, pre_frame) >= 0)
            {
                if (pre_frame->width <= 0 || pre_frame->height <= 0) {
                    av_frame_unref(pre_frame);
                    continue; 
                }

                AVFrame *frame = pre_frame;
                AVFrame *hw_downloaded_frame = nullptr;
                if(pre_frame->format == hw_pix_fmt)
                {
                    hw_downloaded_frame = av_frame_alloc();
                    int err = av_hwframe_transfer_data(hw_downloaded_frame, pre_frame, 0);
                    if(err<0)
                    {
                        av_frame_free(&hw_downloaded_frame);
                        av_frame_unref(pre_frame);
                        continue;
                    }
                    av_frame_copy_props(hw_downloaded_frame, pre_frame);
                    frame = hw_downloaded_frame;
                }

                if(frame->format == AV_PIX_FMT_YUV420P||frame->format == AV_PIX_FMT_NV12)
                {
                    state->video_frame_queue.push(av_frame_clone(frame));
                }
                else {
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
                }
                if(hw_downloaded_frame)
                {
                    av_frame_free(&hw_downloaded_frame);
                }
                // 每解码完成一帧并压入队列后，清空 frame 给下一次使用，防止大面积内存泄漏
                av_frame_unref(pre_frame);
            }
        }
        av_packet_free(&pkt);
    }
    if (pre_frame) av_frame_free(&pre_frame);
}

bool VideoDecoder::open()
{
    if(!Decoder::open())
    {
        return false;
    }
    
    // 初始化目标视频宽高为解码器原始宽高
    this->out_width = 1920;
    this->out_height = 1080;
    
    return true;
}

static enum AVPixelFormat get_hw_format(AVCodecContext* ctx , const enum AVPixelFormat *pix_fmts)
{
    const enum AVPixelFormat *p;
    VideoDecoder* decoder = (VideoDecoder*)ctx->opaque;
    for(p=pix_fmts;*p !=-1;p++)
    {
        if(*p == decoder->hw_pix_fmt)
        {
            return *p;
        }
    }
    return AV_PIX_FMT_NONE;
}

void VideoDecoder::pre_codec_open(const AVCodec* codec)
{
    codec_ctx->opaque = this;
    for(int i=0;;i++)
    {
        const AVCodecHWConfig *config = avcodec_get_hw_config(codec, i);
        if(!config)
        {
            break;
        }
        if(config->methods&AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)
        {
            AVHWDeviceType hw_device_type = config->device_type;
            AVBufferRef *hw_device_ctx = nullptr;
            int err = av_hwdevice_ctx_create(&hw_device_ctx, hw_device_type, nullptr, nullptr, 0);
            if(err==0)
            {
                hw_pix_fmt = config->pix_fmt;
                codec_ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);
                codec_ctx->get_format = get_hw_format;
                av_buffer_unref(&hw_device_ctx);
                printf(">>> 硬解初始化成功, 采用设备类型: %s\n", av_hwdevice_get_type_name(hw_device_type));
                break;
            }
        }
    }
}

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
            //清理解码器内部自带的 B帧/P帧 历史参考缓存
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

                // 如果开启了硬件加速，此时 pre_frame 里的数据其实是在显存里,用format来判断
                if(pre_frame->format == hw_pix_fmt)
                {
                    hw_downloaded_frame = av_frame_alloc();

                    // 这里执行耗时的显存拷贝，将数据“下载”深拷贝到物理系统内存中
                    int err = av_hwframe_transfer_data(hw_downloaded_frame, pre_frame, 0);
                    if(err<0)
                    {
                        av_frame_free(&hw_downloaded_frame);
                        av_frame_unref(pre_frame);
                        continue;
                    }
                    // 数据虽然拷过来了，还要把 PTS 时间戳等属性转移过来
                    av_frame_copy_props(hw_downloaded_frame, pre_frame);
                    frame = hw_downloaded_frame;
                }

                //如果格式符合预设,直接入队,否则需要经过格式转换再入队
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

// FFmpeg 解码器的回调函数，用于在解码器初始化时协商最终的像素格式
// 解码器会提供它能够支持的像素格式列表 (pix_fmts)，本回调需要从中挑出一个并返回，否则无法完成初始化
static enum AVPixelFormat get_hw_format(AVCodecContext* ctx , const enum AVPixelFormat *pix_fmts)
{
    const enum AVPixelFormat *p;
    // 获取挂载在内部的 VideoDecoder 实体对象指针
    VideoDecoder* decoder = (VideoDecoder*)ctx->opaque;
    
    // 遍历解码器支持的格式列表
    for(p=pix_fmts;*p !=-1;p++)
    {
        // 如果列表中有我们之前在 pre_codec_open 里确定的硬解格式（例如 AV_PIX_FMT_CUDA），毫不犹豫选用它
        if(*p == decoder->hw_pix_fmt)
        {
            return *p;
        }
    }
    // 如果兜了一圈也没找到咱们要的硬解格式，则回退/失败（返回 NONE 放弃硬解，由 FFmpeg 自动回退软解）
    return AV_PIX_FMT_NONE;
}

// 在 avcodec_open2 打开解码器真正的执行引擎之前，做的前置探路与预配置
// 核心目的：向系统查询并激活硬件解码加速（寻找显卡支持），这是非常标准且经典的探测流程
void VideoDecoder::pre_codec_open(const AVCodec* codec)
{
    // 将当前解码器类的 this 指针“走私”进 FFmpeg 结构体中，方便后续在 get_hw_format 回调里面把对象捞出来
    codec_ctx->opaque = this;
    
    // 遍历该解码器（如 H.264/HEVC）所能支持的所有硬件加速方案，找一个当前机器能跑的
    for(int i=0;;i++)
    {
        const AVCodecHWConfig *config = avcodec_get_hw_config(codec, i);
        if(!config)
        {
            // 遍历到了尽头，仍然没成功，说明该设备不具备此视频编码的硬解能力，悄悄退出，让系统按默认走 CPU 软解
            break;
        }
        
        // 筛选：确定该硬件配置是否支持通过“硬件设备上下文”提供加速 (这是当今主流硬解 API 标准)
        if(config->methods&AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)
        {
            AVHWDeviceType hw_device_type = config->device_type;
            AVBufferRef *hw_device_ctx = nullptr;
            
            // 尝试创建对应类型的硬件设备（底层实质可能在对接 NVIDIA NVDEC，Intel QSV，或 DXVA2）
            int err = av_hwdevice_ctx_create(&hw_device_ctx, hw_device_type, nullptr, nullptr, 0);
            if(err==0) // 创建毫无意外地成功了！
            {
                // 1. 记下底牌：记录下这种硬解输出的数据载体格式到底是什么（比如用 CUDA 解，输出必然是 CUDA 格式缓存）
                hw_pix_fmt = config->pix_fmt;
                
                // 2. 将刚刚握手成功的显卡会话，挂载到解码器大总管(codec_ctx)上，引擎启动时会自动利用这块显卡
                codec_ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);
                
                // 3. 换心战术：更换格式协商回调。等解码器正式开工询问需要吐出什么格式时，用这个函数塞给它我们硬解要用的格式
                codec_ctx->get_format = get_hw_format;
                
                // 解码器已经拿去克隆(ref)并托管了一份硬件上下文，咱们手里这一份作为“临时工”可以释放了，防内存泄漏
                av_buffer_unref(&hw_device_ctx);
                
                printf(">>> 硬解初始化成功, 采用设备类型: %s\n", av_hwdevice_get_type_name(hw_device_type));
                
                // 找到一种硬解成功（FFmpeg 排在前面的通常是系统里最佳选择），果断见好就收，终止暴力探测
                break;
            }
        }
    }
}

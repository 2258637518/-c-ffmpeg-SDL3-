#include "demuxer.h"
#include "media_state.h"
#include <cstddef>
#include<iostream>
#include <thread>
extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/codec_id.h>
#include <libavcodec/codec_par.h>
#include <libavcodec/packet.h>
#include <libavutil/avutil.h>
#include <libavutil/dict.h>
}
Demuxer::Demuxer(MediaState *state)
{
    this->state = state;
    this->running=true;
}
bool Demuxer::open()
{
    int ret =0;
    ret = avformat_open_input(&state->fmt_ctx ,state->filename.c_str(), NULL,NULL);
    if(ret<0)
    {
        avformat_close_input(&state->fmt_ctx);
         return false;
    }
    ret = avformat_find_stream_info(state->fmt_ctx, NULL);
    if(ret<0) return false;

    state->video_stream_idx=av_find_best_stream(state->fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    state->audio_stream_idx=av_find_best_stream(state->fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    
    if (state->video_stream_idx >= 0) {
        state->video_time_base = state->fmt_ctx->streams[state->video_stream_idx]->time_base;
    }
    if (state->audio_stream_idx >= 0) {
        state->audio_time_base = state->fmt_ctx->streams[state->audio_stream_idx]->time_base;
    }

    return true;
}

void Demuxer::print_metadata()
{
    AVDictionaryEntry *tag = NULL;
    // 必须加上 tag = 才能把返回值赋给 tag 变量
    while((tag = av_dict_get(state->fmt_ctx->metadata, "", tag, AV_DICT_IGNORE_SUFFIX )))
    {
        std::cout<<tag->key<<"="<<tag->value<<std::endl;
    }
    std::cout<<"格式:"<<state->fmt_ctx->iformat->name<<std::endl;
    std::cout<<"时长:"<<state->fmt_ctx->duration/AV_TIME_BASE<<"秒"<<std::endl;
    std::cout<<"码率:"<<state->fmt_ctx->bit_rate/1000<<"kbps"<<std::endl;
    std::cout<<"流个数:"<<state->fmt_ctx->nb_streams<<std::endl;
    for(uint i=0;i<state->fmt_ctx->nb_streams;i++)
    {
        AVStream *stream=state->fmt_ctx->streams[i];
        AVCodecParameters *codec_par=stream->codecpar;
        if(codec_par->codec_type==AVMEDIA_TYPE_AUDIO)
        {
            std::cout<<"音频编码格式:"<<avcodec_get_name(codec_par->codec_id)<<std::endl;
            std::cout<<"通道数:"<<codec_par->ch_layout.nb_channels<<std::endl;
            std::cout<<"采样率:"<<codec_par->sample_rate<<std::endl;
        }
        else if(codec_par->codec_type==AVMEDIA_TYPE_VIDEO)
        {
            std::cout<<"视频编码格式:"<<avcodec_get_name(codec_par->codec_id)<<std::endl;
            std::cout<<"分辨率:"<<codec_par->width<<"x"<<codec_par->height<<std::endl;
        }
    }
}

void Demuxer::start()
{
    running=true;
    read_thread=std::thread (&Demuxer::read_thread_loop,this);
}

void Demuxer::stop()
{
    running=false;
    state->video_pkt_queue.abort();
    state->audio_pkt_queue.abort();
    if(read_thread.joinable())
    {
        read_thread.join();
    }
}

Demuxer::~Demuxer()
{
    if(running)
    {
        Demuxer::stop();
    }
}

void Demuxer::read_thread_loop()
{
    while(running)
    {
        // 每次循环必须分配一个新的 packet，因为压入队列后它的所有权就转移给解码器了
        AVPacket *pkt = av_packet_alloc();
        if(av_read_frame(state->fmt_ctx, pkt)>=0)
        {
            if(pkt->stream_index==state->video_stream_idx)
            {
                while(state->video_pkt_queue.size()>=75&&running)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                state->video_pkt_queue.push(pkt);
            }
            else if(pkt->stream_index==state->audio_stream_idx)
            {   
                while(state->audio_pkt_queue.size()>=75&&running)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                state->audio_pkt_queue.push(pkt);
            }
            else 
            {
                // 用不上这个流的包，释放掉
                av_packet_free(&pkt);
            }  
        }
        else {
            // 读取结束或失败
            av_packet_free(&pkt);
            break;
        }

    }
}
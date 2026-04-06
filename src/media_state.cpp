#include "media_state.h"
#include<iostream>
extern "C"{
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
}
MediaState::MediaState(const std::string& filename):
filename(filename),
state(PlayState::STOPPED),
fmt_ctx(nullptr),
video_stream_idx(-1),
audio_stream_idx(-1),
seek_req(false),
seek_pos(0),
audio_need_clear(false)
{
}
MediaState::~MediaState()
{
    if(fmt_ctx)
    {
        avformat_close_input(&fmt_ctx);
        fmt_ctx = nullptr;
    }

    AVPacket *pkt = nullptr;
    while(video_pkt_queue.size()>0)
    {
        pkt = video_pkt_queue.pop();
        av_packet_free(&pkt);
    }

    while(audio_pkt_queue.size()>0)
    {
        pkt = audio_pkt_queue.pop();
        av_packet_free(&pkt);
    }

    AVFrame *frame = nullptr;
    while(video_frame_queue.size()>0)
    {
        frame = video_frame_queue.pop();
        av_frame_free(&frame);
    }

    while(audio_frame_queue.size()>0)
    {
        frame = audio_frame_queue.pop();
        av_frame_free(&frame);
    }
    std::cout<<"MediaState 资源清理完毕。"<<std::endl;
}
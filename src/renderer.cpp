#include "renderer.h"
#include <iostream>
#include "media_state.h"
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_oldnames.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>
#include <cstddef>
extern "C"{
#include <libavutil/frame.h>
#include <libavutil/rational.h>
#include <libavutil/samplefmt.h>
}
Renderer::Renderer(MediaState *state)
{
    this->state=state;
    this->window=nullptr;
    this->renderer=nullptr;
    this->texture=nullptr;
    this->audio_stream=nullptr;
    this->audio_device=0;
    this->keepRunning=false;
}
Renderer::~Renderer()
{
    this->close();
}
bool Renderer::init()
{
    if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO))
    {
        return false;
    }
    int video_w = 1600;
    int video_h = 900;
    if (state->video_stream_idx >= 0) {
        AVCodecParameters *codec_par = state->fmt_ctx->streams[state->video_stream_idx]->codecpar;
        video_w = codec_par->width;
        video_h = codec_par->height;
    }
    this->window=SDL_CreateWindow("my_video_player", video_w, video_h, SDL_WINDOW_RESIZABLE);
    if(!window)
    {
        this->close();
        return false;
    }
    this->renderer=SDL_CreateRenderer(this->window, NULL);
    if(!renderer)
    {
        this->close();
        return false;
    }
    // Texture 必须与视频解码出的一帧大小完全一致
    this->texture=SDL_CreateTexture(this->renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, video_w, video_h);
    if(!texture)
    {
        this->close();
        return false;
    }
    SDL_AudioSpec spec;
    spec.format=SDL_AUDIO_F32;
    spec.channels =2;
    spec.freq=44100;
    this->audio_device=SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
    if(audio_device==0)
    {
        this->close();
        return false;
    }
    audio_stream = SDL_CreateAudioStream(&spec, &spec);//快速建立音频通道,先使用默认参数初始化
    if(!audio_stream)
    {
        this->close();
        return false;
    }
    SDL_BindAudioStream(this->audio_device, this->audio_stream);
    return true;
}
void Renderer::render_audio()
{
    if(keepRunning)
    {
        return;
    }
    keepRunning=true;
    feed_audio = std::thread([this](){
        while(keepRunning)
        {
            feed_audio_stream();
        }
    });
}
void Renderer::render_video()
{
    if(!pending_video_frame)
    {
        if(state->video_frame_queue.size()<=0)
        {
            return;
        }
        pending_video_frame = state->video_frame_queue.pop();
        if(!pending_video_frame)
        {
            return;
        }
    }
    
    double video_pts=pending_video_frame->pts*av_q2d(state->video_time_base);
    double audio_pts=state->audio_clock.get_clock();
    double diff=video_pts-audio_pts;
    double sync_threshold=0.01;
    
    if(diff>sync_threshold)
    {
        // Not time yet, wait until next loop iteration
        return;
    }
    else if(diff<-0.1)
    {
        av_frame_free(&pending_video_frame);
        pending_video_frame = nullptr;
        return;
    }
    SDL_UpdateYUVTexture(texture, NULL, pending_video_frame->data[0], pending_video_frame->linesize[0], pending_video_frame->data[1],pending_video_frame->linesize[1] ,pending_video_frame->data[2] ,pending_video_frame->linesize[2] );
    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
    av_frame_free(&pending_video_frame);
    pending_video_frame = nullptr;
}
void Renderer::feed_audio_stream()
{
    if(!audio_stream)
    {
        return;
    }
    if(SDL_GetAudioStreamQueued(audio_stream)>32768)
    {
        return;
    }
    if(state->audio_frame_queue.size()<=0)
    {
        return;
    }
    AVFrame *frame= state->audio_frame_queue.pop();
    
    if (frame->pts != AV_NOPTS_VALUE)
    {
        double pts = frame->pts * av_q2d(state->audio_time_base);
        // Estimate the delay caused by buffered data in SDL
        int queued_bytes = SDL_GetAudioStreamQueued(audio_stream);
        // spec.freq=44100, spec.channels=2, spec.format=SDL_AUDIO_F32 (4 bytes per sample)
        // bytes_per_sec = 44100 * 2 * 4 = 352800 
        double delay = (double)queued_bytes / 352800.0;
        state->audio_clock.set_clock(pts - delay);
    }

    int data_size= av_samples_get_buffer_size(nullptr, frame->ch_layout.nb_channels, frame->nb_samples, (AVSampleFormat)frame->format, 1);
    if(data_size>0)
    {
        SDL_PutAudioStreamData(audio_stream, frame->data[0], data_size);
    }
    av_frame_free(&frame);
}
void Renderer::close()
{
    if(keepRunning)
    {
        keepRunning=false;
        if(feed_audio.joinable())
        {
            feed_audio.join();
        }
    }
    if(pending_video_frame) { av_frame_free(&pending_video_frame); pending_video_frame = nullptr; }
    if(audio_stream) { SDL_DestroyAudioStream(this->audio_stream); this->audio_stream = nullptr; }
    if(audio_device) { SDL_CloseAudioDevice(this->audio_device); this->audio_device = 0; }
    if(texture) { SDL_DestroyTexture(this->texture); this->texture = nullptr; }
    if(renderer) { SDL_DestroyRenderer(this->renderer); this->renderer = nullptr; }
    if(window) { SDL_DestroyWindow(this->window); this->window = nullptr; }
    SDL_Quit();
}
bool Renderer::should_quit()
{
    SDL_Event event;
    while(SDL_PollEvent(&event))
    {
        if(event.type==SDL_EVENT_QUIT)
        {
            return true;
        }
    }
    return false;
}
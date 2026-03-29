#include "renderer.h"

#include "media_state.h"
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
    return true;
}
void Renderer::render_video(double current_audio_delay)
{
    if(!pending_video_frame)
    {
        if(state->video_frame_queue.size()<=0)
        {
            SDL_Delay(1);
            return;
        }
        pending_video_frame = state->video_frame_queue.pop();
        if(!pending_video_frame)
        {
            return;
        }
    }
    
    double video_pts=pending_video_frame->pts*av_q2d(state->video_time_base);
    double logical_audio_pts=state->audio_clock.get_clock();
    double real_audio_pts = logical_audio_pts-current_audio_delay;
    SyncDecision decision = sync_manager.decide_video_frame(video_pts, real_audio_pts);
    
    if(decision.action == SyncAction::Wait)
    {
        // Must sleep to avoid 100% CPU usage
        SDL_Delay(1);
        return;
    }
    else if(decision.action == SyncAction::Drop)
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
void Renderer::close()
{
    if(keepRunning)
    {
        keepRunning=false;
    }
    if(pending_video_frame) { av_frame_free(&pending_video_frame); pending_video_frame = nullptr; }
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
#include "renderer.h"

#include "media_state.h"
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_oldnames.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>
#include <cstddef>
#include <cstdint>
extern "C"{
#include <libavutil/frame.h>
#include <libavutil/rational.h>
#include <libavutil/samplefmt.h>
#include <libavutil/avutil.h>
}
Renderer::Renderer(MediaState *state)
{
    this->state=state;
    this->window=nullptr;
    this->renderer=nullptr;
    this->texture=nullptr;
    this->keepRunning=false;
    this->user_provide_pts=-0.08;
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
    double real_audio_pts = logical_audio_pts-current_audio_delay+user_provide_pts;
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
    draw_progress_bar();
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
        else if(event.type == SDL_EVENT_KEY_DOWN)
        {
            if (event.key.key == SDLK_J) {
                user_provide_pts -= 0.05; 
            }
            else if (event.key.key == SDLK_K) {
                user_provide_pts += 0.05;
            }
            else if (event.key.key == SDLK_SPACE) {
                if(state->state==PlayState::PLAYING)
                {
                    state->state=PlayState::PAUSE;
                }
                else if(state->state==PlayState::PAUSE)
                {
                    state->state=PlayState::PLAYING;
                }
            }
            else if(event.key.key == SDLK_RIGHT)
            {
               double target_sec = 0.0;
                if (state->seek_req.load()) {
                    // 底层还没处理完，直接基于上一次定下的目标时间继续累加
                    target_sec = (double)state->seek_pos.load() / AV_TIME_BASE + 5.0;
                } else {
                    // 底层是正常状态，基于现在的播放进度去加
                    target_sec = state->audio_clock.get_clock() + 5.0;
                }
                execute_seek((int64_t)(target_sec * AV_TIME_BASE));
            }
            else if(event.key.key == SDLK_LEFT)
            {
                double target_sec = 0.0;
                if (state->seek_req.load()) {
                    target_sec = (double)state->seek_pos.load() / AV_TIME_BASE - 5.0;
                } else {
                    target_sec = state->audio_clock.get_clock() - 5.0;
                }
                if(target_sec<0.0)
                {
                    target_sec=0.0; 
                }
                execute_seek((int64_t)(target_sec * AV_TIME_BASE));
            }
        }
        else if(event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
        {
            int w,h;
            SDL_GetWindowSize(window, &w, &h);
            if(event.button.y>h-30)
            {
                double percentage = (double)event.button.x/(double)w;
                if(state->fmt_ctx&&state->fmt_ctx->duration>0)
                {
                    int64_t target_pos = (int64_t)(percentage*state->fmt_ctx->duration);
                    execute_seek(target_pos);
                }
            }
           
        }
    }
    return false;
}
void Renderer::execute_seek(int64_t target_pos)
{
     // 告知需要跳转的位置和发起跳转请求
    state->seek_pos = target_pos;
    state->seek_req = true;
    // 清空声音残留
    state->audio_need_clear = true;
    
    // 清空由于延迟遗留在此处的旧视频画面
    if (pending_video_frame) {
        av_frame_free(&pending_video_frame);
        pending_video_frame = nullptr;
    }
}
void Renderer::draw_progress_bar()
{
    int w,h;
    SDL_GetRenderOutputSize(renderer, &w, &h);
    SDL_FRect bg_rect = {0.0f,(float)(h-30),(float)w,30.0f};
    SDL_SetRenderDrawColor(renderer, 50, 50, 50, 180);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND); 
    SDL_RenderFillRect(renderer, &bg_rect);

    double duration_sec = (double)state->fmt_ctx->duration/AV_TIME_BASE;
    double current_sec = state->audio_clock.get_clock();
    float progress = (float)((current_sec/duration_sec)*w);
    SDL_FRect fg_rect = {0.0f,(float)(h-30),progress,30.0f};
    SDL_SetRenderDrawColor(renderer, 255, 100, 100, 255);
    SDL_RenderFillRect(renderer, &fg_rect);
}
#include "renderer.h"
#include <iostream>
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
    this->is_debouncing = false;
    this->debounce_target_pos = 0;
    this->last_keypress_time = 0;
}
Renderer::~Renderer()
{
    this->close();
}
bool Renderer::init()
{
    //启动SDL视频和音频渲染模块
    if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO))
    {
        return false;
    }

    //预设的窗口大小
    int window_w = 1600;
    int window_h = 900;
    
    this->window=SDL_CreateWindow("my_video_player", window_w, window_h, SDL_WINDOW_RESIZABLE);
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
    
    return true;
}
void Renderer::render_video(double current_audio_delay)//关键部分:根据各种条件控制视频帧的渲染
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
        if(state->global_seek_serial.load()!=state->video_decoder_serial.load())
        {
            av_frame_free(&pending_video_frame);
            pending_video_frame=nullptr;
            return;
        }
        double target_sec = (double)state->seek_pos.load()/AV_TIME_BASE;
        double video_pts = pending_video_frame->pts*av_q2d(state->video_time_base);
        if(video_pts<target_sec)
        {
            av_frame_free(&pending_video_frame);
            pending_video_frame = nullptr;
            return;
        }
    }
    
    double video_pts=pending_video_frame->pts*av_q2d(state->video_time_base);
    double current_speed = state->player_speed.load();
    double logical_audio_pts=state->audio_clock.get_clock(current_speed);
    // 扣除硬件延迟时，必须考虑真实的物理延迟转为逻辑资源的时间
    double real_audio_pts = logical_audio_pts - current_audio_delay * current_speed + user_provide_pts;

    //根据真实差值由decision做决策
    SyncDecision decision = sync_manager.decide_video_frame(video_pts, real_audio_pts);
    if(decision.action == SyncAction::Wait)
    {
        SDL_Delay(1);
        return;
    }
    else if(decision.action == SyncAction::Drop)
    {
        av_frame_free(&pending_video_frame);
        pending_video_frame = nullptr;
        return;
    }
    //仅在第一次或者视频帧的格式变化后才进行texture的创建
    if(need_change(pending_video_frame))
    {
        texture = SDL_CreateTexture(renderer, target_sdl_fmt, SDL_TEXTUREACCESS_STREAMING, pending_video_frame->width, pending_video_frame->height);
        current_tex_w = pending_video_frame->width;
        current_tex_h = pending_video_frame->height;
        current_tex_fmt = target_sdl_fmt;
        if(!texture)
        {
            return;
        }
    }

    //根据不同的格式选择不同的texture更新方式
    if(pending_video_frame->format==AV_PIX_FMT_YUV420P)
    {
        SDL_UpdateYUVTexture(texture, NULL, pending_video_frame->data[0], pending_video_frame->linesize[0], pending_video_frame->data[1],pending_video_frame->linesize[1] ,pending_video_frame->data[2] ,pending_video_frame->linesize[2] );
    }
    else if(pending_video_frame->format==AV_PIX_FMT_NV12)
    {
        SDL_UpdateNVTexture(texture, NULL, pending_video_frame->data[0], pending_video_frame->linesize[0], pending_video_frame->data[1], pending_video_frame->linesize[1]);
    }

    //在拉伸窗口的过程中保持视频画面的协调
    int win_w,win_h;
    SDL_GetRenderOutputSize(renderer,&win_w,&win_h);
    int frame_w = pending_video_frame->width;
    int frame_h = pending_video_frame->height;
    float scale_x = (float)win_w/(float)frame_w;
    float scale_y = (float)win_h/(float)frame_h;
    float scale = (scale_x<scale_y)?scale_x:scale_y;
    float dest_w = frame_w*scale;
    float dest_h = frame_h*scale;
    float dest_x = (win_w - dest_w)/2.0f;
    float dest_y = (win_h - dest_h)/2.0f;
    SDL_FRect dest_rect = {dest_x,dest_y,dest_w,dest_h};
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 100);
    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, texture, NULL, &dest_rect);
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
bool Renderer::should_quit()//控制程序结束以及事件判断
{
    SDL_Event event;
    while(SDL_PollEvent(&event))
    {
        if(event.type==SDL_EVENT_QUIT)
        {
            return true;
        }
        else if(event.type == SDL_EVENT_KEY_DOWN)//按键事件
        {
            //允许手动调整音视频延迟
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
                if(!is_debouncing)
                {
                    debounce_target_pos = (int64_t)(state->audio_clock.get_clock()*AV_TIME_BASE);
                    is_debouncing = true;
                }
                debounce_target_pos +=5LL*AV_TIME_BASE;
                last_keypress_time = SDL_GetTicks();
            }
            else if(event.key.key == SDLK_LEFT)
            {
                if(!is_debouncing)
                {
                    debounce_target_pos = (int64_t)(state->audio_clock.get_clock()*AV_TIME_BASE);
                    is_debouncing = true;
                }
                debounce_target_pos -= 5LL * AV_TIME_BASE;
                if(debounce_target_pos < 0) debounce_target_pos = 0;
                last_keypress_time = SDL_GetTicks();
            }
            //设置音量,映射在0-100的范围内
            else if(event.key.key == SDLK_UP)
            {
                state->audio_volume +=5;
                if(state->audio_volume>100)
                {
                    state->audio_volume=100;
                }
                else if(state->audio_volume<0)
                {
                    state->audio_volume=0;
                }
                std::cout<<"当前音量:"<<state->audio_volume<<"%"<<std::endl;
                
            }
            else if(event.key.key == SDLK_DOWN)
            {
                state->audio_volume -=5;
                if(state->audio_volume>100)
                {
                    state->audio_volume=100;
                }
                else if(state->audio_volume<0)
                {
                    state->audio_volume=0;
                }
                std::cout<<"当前音量:"<<state->audio_volume<<"%"<<std::endl;
            }
            //设置视频播放倍速
            else if(event.key.key == SDLK_LEFTBRACKET)
            {
                double cur = state->player_speed.load();
                state->player_speed.store(cur-0.5);
            }
            else if(event.key.key == SDLK_RIGHTBRACKET)
            {
                double cur = state->player_speed.load();
                state->player_speed.store(cur+0.5);
            }
        }
        else if(event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)//根据鼠标指针位置进行进度条跳跃
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
    if (is_debouncing && (SDL_GetTicks() - last_keypress_time > 200)) //引入延迟判断机制,不必时刻判断快进/快退
    {
    execute_seek(debounce_target_pos);
    is_debouncing = false;
    }
    return false;
}
void Renderer::execute_seek(int64_t target_pos)
{
    //全局序列++,音频和视频序列等待冲刷完成后再自行++
    state->global_seek_serial++;
    double target_sec = (double)target_pos / AV_TIME_BASE;

    state->audio_clock.set_clock(target_sec);
    state->video_clock.set_clock(target_sec);
     // 告知需要跳转的位置和发起跳转请求
    state->seek_pos = target_pos;
    state->seek_req = true;
    // 清空声音残留
    state->audio_need_clear = true;
    
    state->video_frame_queue.flush([](AVFrame* &f){ av_frame_free(&f); });
    state->audio_frame_queue.flush([](AVFrame* &f){ av_frame_free(&f); });

    // 清空由于延迟遗留在此处的旧视频画面
    if (pending_video_frame) {
        av_frame_free(&pending_video_frame);
        pending_video_frame = nullptr;
    }
}
void Renderer::draw_progress_bar()//画进度条
{
    int w,h;
    SDL_GetRenderOutputSize(renderer, &w, &h);
    SDL_FRect bg_rect = {0.0f,(float)(h-30),(float)w,30.0f};
    SDL_SetRenderDrawColor(renderer, 50, 50, 50, 180);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND); 
    SDL_RenderFillRect(renderer, &bg_rect);

    double duration_sec = (double)state->fmt_ctx->duration/AV_TIME_BASE;
    double current_sec = is_debouncing 
            ? ((double)debounce_target_pos / AV_TIME_BASE) 
            : state->audio_clock.get_clock();
    float progress = (float)((current_sec/duration_sec)*w);
    SDL_FRect fg_rect = {0.0f,(float)(h-30),progress,30.0f};
    SDL_SetRenderDrawColor(renderer, 255, 100, 100, 255);
    SDL_RenderFillRect(renderer, &fg_rect);
}
bool Renderer::need_change(AVFrame *pending_video_frame)//根据不同的格式创建不同的texture
{
    if (pending_video_frame->format == AV_PIX_FMT_YUV420P) {
        target_sdl_fmt = SDL_PIXELFORMAT_IYUV;
    } 
    else if (pending_video_frame->format == AV_PIX_FMT_NV12) {
        target_sdl_fmt = SDL_PIXELFORMAT_NV12;
    }
    if(texture==nullptr
        ||current_tex_w != pending_video_frame->width
        ||current_tex_h!=pending_video_frame->height
        ||current_tex_fmt!=target_sdl_fmt)
    {
        if(texture!=NULL)
        {
            SDL_DestroyTexture(texture);
        }
        return true;
    }
    return false;
}
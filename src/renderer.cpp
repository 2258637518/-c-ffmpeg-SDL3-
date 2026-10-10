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
    this->gl_ctx = nullptr;
    this->modern_render = nullptr;
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

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    //预设的窗口大小
    int window_w = 1600;
    int window_h = 900;
    
    this->window = SDL_CreateWindow("my_video_player", window_w, window_h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if(!window)
    {
        this->close();
        return false;
    }

    this->gl_ctx = SDL_GL_CreateContext(window);
    if(!gl_ctx)
    {
        std::cerr << "gl_ctx创建失败";
        this->close();
        return false;
    }
    SDL_GL_MakeCurrent(this->window, this->gl_ctx);

    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        std::cerr << "Failed to initialize Glad in SDL context!" << std::endl;
        this->close();
        return false;
    }

    this->modern_render = std::make_unique<ModernRender>();
    if (!this->modern_render->Init()) {
        std::cerr << "ModernRender创建失败" << std::endl;
        this->close();
        return false;
    }

    this->modern_render->Resize(window_w, window_h);

    // 开启垂直同步（防止画面撕裂）
    SDL_GL_SetSwapInterval(1);
    
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

    if (this->modern_render)
    {
        modern_render->RenderFrame(pending_video_frame);
    }
    draw_progress_bar();

    SDL_GL_SwapWindow(this->window);

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
    if (modern_render) { this->modern_render.reset();}
    if (gl_ctx) { SDL_GL_DestroyContext(this->gl_ctx); this->gl_ctx = nullptr; }
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
        else if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
        {
            int new_w = 0;
            int new_h = 0;
            SDL_GetWindowSize(window, &new_w, &new_h);
            this->on_window_resized(new_w,new_h);
        }
    }
    if (is_debouncing && (SDL_GetTicks() - last_keypress_time > 200)) //引入延迟判断机制,不必时刻判断快进/快退
    {
    execute_seek(debounce_target_pos);
    is_debouncing = false;
    }
    return false;
}
void Renderer::on_window_resized(int w,int h)
{
    if (modern_render)
    {
        modern_render->Resize(w, h);
    }
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
    if (!this->window || !state->fmt_ctx || state->fmt_ctx->duration <= 0) {
        return;
    }

    // 1. 获取窗口的实际像素大小（适配高分屏）
    int win_w = 0, win_h = 0;
    SDL_GetWindowSizeInPixels(this->window, &win_w, &win_h);
    if (win_w <= 0 || win_h <= 0) return;

    double duration_sec = (double)state->fmt_ctx->duration / AV_TIME_BASE;
    double current_sec = is_debouncing
        ? ((double)debounce_target_pos / AV_TIME_BASE)
        : state->audio_clock.get_clock();

    if (current_sec < 0.0) current_sec = 0.0;
    if (current_sec > duration_sec) current_sec = duration_sec;

    float progress_ratio = static_cast<float>(current_sec / duration_sec);
    int progress_w = static_cast<int>(progress_ratio * win_w);

    const int bar_h = 10;

    glEnable(GL_SCISSOR_TEST);

    glScissor(0, 0, win_w, bar_h);
    glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    
    if (progress_w > 0) {
        glScissor(0, 0, progress_w, bar_h);
        glClearColor(1.0f, 0.25f, 0.25f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    glDisable(GL_SCISSOR_TEST);
}
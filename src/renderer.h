#pragma once 
#include <glad/glad.h>
#include "media_state.h"
#include "syncmanager.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>
#include <cstdint>
#include <thread>
#include <atomic>
#include "render/modern_render.h"
#include <memory>

class Renderer{
private:
    MediaState *state;
    SDL_Window *window;

    SDL_GLContext gl_ctx;
    std::unique_ptr<ModernRender> modern_render;

    AVFrame *pending_video_frame = nullptr;
    SyncManager sync_manager;
    std::atomic<bool> keepRunning;
    double user_provide_pts;
    void execute_seek(int64_t target_pos);
    void draw_progress_bar();
    bool is_debouncing;
    int64_t debounce_target_pos;
    uint64_t last_keypress_time;

public:
    Renderer(MediaState *state);
    ~Renderer();

    bool init();
    void render_video(double hw_delay);
    void close();
    bool should_quit();
    void on_window_resized(int w,int h);

};
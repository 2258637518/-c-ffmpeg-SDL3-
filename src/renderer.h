#pragma once 
#include "media_state.h"
#include "syncmanager.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_video.h>
#include <thread>
#include <atomic>
class Renderer{
private:
    MediaState *state;
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    AVFrame *pending_video_frame = nullptr;
    SyncManager sync_manager;
    std::atomic<bool> keepRunning;
    double user_provide_pts;
public:
    Renderer(MediaState *state);
    ~Renderer();

    bool init();
    void render_video(double hw_delay);
    void close();
    bool should_quit();
};
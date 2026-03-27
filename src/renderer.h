#pragma once 
#include "media_state.h"
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
    SDL_AudioStream *audio_stream;
    SDL_AudioDeviceID audio_device;
    AVFrame *pending_video_frame = nullptr;

    std::atomic<bool> keepRunning;
    std::thread feed_audio;
    void feed_audio_stream();
public:
    Renderer(MediaState *state);
    ~Renderer();

    bool init();
    void render_audio();
    void render_video();
    void close();
    bool should_quit();
};
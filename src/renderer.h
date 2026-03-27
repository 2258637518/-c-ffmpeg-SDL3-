#pragma once 
#include "media_state.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_video.h>
class Renderer{
private:
    MediaState *state;
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    SDL_AudioStream *audio_stream;
    SDL_AudioDeviceID audio_device;
    AVFrame *pending_video_frame = nullptr;
public:
    Renderer(MediaState *state);
    ~Renderer();

    bool init();
    void render_video();
    void feed_audio_stream();
    void close();
    bool should_quit();
};
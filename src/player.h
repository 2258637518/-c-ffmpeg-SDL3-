#pragma once
#include <memory>
#include "audio_output.h"
#include "demuxer.h"
#include "media_state.h"
#include "renderer.h"
#include "audio_decoder.h"
#include "video_decoder.h"
#include <string>
class Player{
public:
    explicit Player(const std::string&filepath);
    ~Player();
    bool open();
    void start();
    void tick();
    void stop();

    bool should_quit();

    static bool sdl_event_watch(void* userdata, union SDL_Event* event);

private:
    MediaState state;
    Demuxer demuxer;
    Renderer renderer;
    AudioOutput audio_output;
    std::unique_ptr<AudioDecoder> audio_decoder;
    std::unique_ptr<VideoDecoder> video_decoder;
    PlayState last_state = PlayState::STOPPED;
    bool opened = false;
    bool started = false;
    bool quit=false;
};
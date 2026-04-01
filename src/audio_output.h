#pragma once
#include "media_state.h"
#include <SDL3/SDL_audio.h>
#include <atomic>
#include <thread>
class AudioOutput{
private:
    MediaState *state;
    SDL_AudioStream *audio_stream;
    SDL_AudioDeviceID audio_device;
    std::thread feed_audio_thread;
    std::atomic<bool> keepRunning;
    void feed_audio_loop();

public:
    AudioOutput(MediaState *state);
    ~AudioOutput();
    bool init();
    void start();
    void stop();
    void close();
    void set_pause(bool is_pause);
    double get_audio_delay();
};
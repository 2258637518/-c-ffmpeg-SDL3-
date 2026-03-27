#pragma once
#include "media_state.h"
#include<thread>
#include<string>
#include<map>

class Demuxer{
    private:
    void read_thread_loop();
    MediaState *state;
    std::thread read_thread;
    bool running;

    public:
    Demuxer (MediaState *state);
    ~Demuxer();
    bool open();
    void print_metadata();
    void start();
    void stop();

};

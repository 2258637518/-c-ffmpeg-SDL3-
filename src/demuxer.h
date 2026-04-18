#pragma once
#include "media_state.h"
#include<thread>

//负责解封装
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
    void print_metadata();//打印元数据
    void start();
    void stop();
};

#pragma once
#include <mutex>
#include <SDL3/SDL.h>
#include <SDL3/SDL_timer.h>
class MediaClock{
private:
    double pts;
    double last_updata;
    bool is_paused;
    mutable std::mutex mtx;
    double get_system_time()
    {
        return SDL_GetTicksNS()/1000000000.0;
    }
public:
    MediaClock()
    {
        pts=0.0;
        last_updata=get_system_time();
        is_paused=false;
    }
    void set_clock(double new_pts)
    {
        std::lock_guard<std::mutex>lock(mtx);
        pts=new_pts;
        last_updata=get_system_time();
    }
    double get_clock()
    {
        std::lock_guard<std::mutex>lock(mtx);
        if(is_paused)
        {
            return pts;
        }
        double time_diff=get_system_time()-last_updata;
        return pts+time_diff;
    }
    void set_paused(bool paused)
    {
        std::lock_guard<std::mutex>lock(mtx);
        if(is_paused&&!paused)
        {
            last_updata=get_system_time();
        }
        is_paused=paused;
    }
};
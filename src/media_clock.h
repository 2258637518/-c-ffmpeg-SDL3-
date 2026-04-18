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
    double get_system_time()//获取系统时间
    {
        //为什么引入系统时间?
        //计算机分段处理音频,音频时间戳呈阶梯变化,精度不如视频帧的pts,引入系统时间来解决画面卡顿的问题
        return SDL_GetTicksNS()/1000000000.0;
    }
public:
    MediaClock()
    {
        pts=0.0;
        last_updata=get_system_time();
        is_paused=false;
    }
    void set_clock(double new_pts)//设置当前音频时间戳
    {
        std::lock_guard<std::mutex>lock(mtx);
        pts=new_pts;
        last_updata=get_system_time();//更新当前pts设立的系统时间
    }
    double get_clock(double speed = 1.0)
    {
        std::lock_guard<std::mutex>lock(mtx);
        if(is_paused)
        {
            return pts;//如果处于暂停状态,返回当前pts依旧显示这一帧画面
        }
        double time_diff=get_system_time()-last_updata;
        return pts + time_diff * speed;
    }
    void set_paused(bool paused)
    {
        std::lock_guard<std::mutex>lock(mtx);
        if(is_paused&&!paused)//解除暂停时也要更新当前系统时间,恢复播放后的前几帧才能正常播放,不会误判追帧
        {
            last_updata=get_system_time();
        }
        is_paused=paused;
    }
};
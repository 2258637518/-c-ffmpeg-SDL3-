#pragma once
enum class SyncAction{
    RenderNow,
    Wait,
    Drop
};//音视频同步逻辑的三种状态

struct SyncDecision{
    SyncAction action;
    double delay_sec = 0.0;
    double dff_sec = 0.0;
};

class SyncManager{
public:
    SyncDecision decide_video_frame(double video_pts, double master_pts) const;//决定当前视频帧去留的函数
    void set_sync_threshold(double value);//设置最大容忍的超前延迟
    void set_drop_threshold(double value);//设置最大容忍落后延迟

private:
    //一般人耳对声音超前更加敏感,因此sync_threshold会比drop_threshold小很多
    double sync_threshold = 0.01;
    double drop_threshold = 0.10;
};
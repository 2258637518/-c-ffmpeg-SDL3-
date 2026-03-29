#pragma once
enum class SyncAction{
    RenderNow,
    Wait,
    Drop
};
struct SyncDecision{
    SyncAction action;
    double delay_sec = 0.0;
    double dff_sec = 0.0;
};
class SyncManager{
public:
    SyncDecision decide_video_frame(double video_pts, double master_pts) const;
    void set_sync_threshold(double value);
    void set_drop_threshold(double value);

private:
    double sync_threshold = 0.01;
    double drop_threshold = 0.10;
};
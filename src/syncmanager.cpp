#include "syncmanager.h"
SyncDecision SyncManager::decide_video_frame(double video_pts,double audio_pts)const
{
    double diff=video_pts-audio_pts;
    if(diff>sync_threshold)
    {
        return {SyncAction::Wait, diff, diff};
    }
    if (diff < -drop_threshold) {
        return {SyncAction::Drop, 0.0, diff};
    }
    return {SyncAction::RenderNow, 0.0, diff};
}
void SyncManager::set_sync_threshold(double value)
{
    this->sync_threshold=value;
}

void SyncManager::set_drop_threshold(double value)
{
    this->drop_threshold=value;
}
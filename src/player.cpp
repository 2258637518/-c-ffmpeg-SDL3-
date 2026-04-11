#include "player.h"
#include "media_state.h"
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_events.h>

bool Player::sdl_event_watch(void* userdata, SDL_Event* event) {
    if (event->type == SDL_EVENT_WINDOW_EXPOSED || 
        event->type == SDL_EVENT_WINDOW_RESIZED || 
        event->type == SDL_EVENT_WINDOW_MOVED) {
        
        Player* player = static_cast<Player*>(userdata);
        if (player && player->started && !player->quit && player->state.state == PlayState::PLAYING) {
            // 系统正在阻塞主线程处理拖拽，我们强插一帧渲染
            double hw_delay = player->audio_output.get_audio_delay();
            player->renderer.render_video(hw_delay);
        }
    }
    return true;
}

Player::Player(const std::string& filepath)
    : state(filepath),
      demuxer(&state),
      renderer(&state),
      audio_output(&state),
      audio_decoder(nullptr),
      video_decoder(nullptr),
      opened(false),
      started(false),
      quit(false)
{
}
Player::~Player()
{
    stop();
}
bool Player::open()
{
    if(opened)return true;
    if(!demuxer.open())return false;
    demuxer.print_metadata();
    audio_decoder = std::make_unique<AudioDecoder>(&state,state.audio_stream_idx);
    video_decoder = std::make_unique<VideoDecoder>(&state,state.video_stream_idx);
    if(!audio_decoder->open())return false;
    if(!video_decoder->open())return false;
    if(!renderer.init())return false;
    if(!audio_output.init())return false;
    
    SDL_AddEventWatch(sdl_event_watch, this);
    
    state.state = PlayState::STOPPED;
    opened = true;
    return true;
}
void Player::start()
{
      if (!opened || started) return;

    demuxer.start();
    audio_decoder->start();
    video_decoder->start();
    audio_output.start();

    state.state = PlayState::PLAYING;
    started = true;
}
void Player::tick()
{
    if (!started || quit) return;

    if (renderer.should_quit()) {
        quit = true;
        return;
    }
    if(state.state!=last_state)
    {
        if(state.state == PlayState::PLAYING)
        {
            audio_output.set_pause(false);
            state.audio_clock.set_paused(false);
            state.video_clock.set_paused(false);
        }
        else if(state.state == PlayState::PAUSE)
        {
            audio_output.set_pause(true);
            state.audio_clock.set_paused(true);
            state.video_clock.set_paused(true);
        }
        last_state=state.state;
    }
    if(state.state==PlayState::PAUSE)
    {
        SDL_Delay(10);
        return;
    }
    double hw_delay=audio_output.get_audio_delay();
    renderer.render_video(hw_delay);
}
bool Player::should_quit()
{
    return quit;
}
void Player::stop()
{
    if (!opened) return;

    if (video_decoder) video_decoder->stop();
    if (audio_decoder) audio_decoder->stop();
    demuxer.stop();
    audio_output.close();
    renderer.close();

    SDL_RemoveEventWatch(sdl_event_watch, this);

    state.state = PlayState::STOPPED;
    started = false;
    opened = false;
}

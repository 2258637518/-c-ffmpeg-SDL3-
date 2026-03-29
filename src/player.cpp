#include "player.h"

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

    state.state = PlayState::STOPPED;
    started = false;
    opened = false;
}

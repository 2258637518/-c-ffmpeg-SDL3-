#include "audio_output.h"
#include "media_state.h"
#include <SDL3/SDL_audio.h>
#include <chrono>
#include <libavutil/avutil.h>
#include <libavutil/rational.h>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
#include <libavutil/channel_layout.h>
}
AudioOutput::AudioOutput(MediaState *state)
{
    this->state=state;
    this->audio_stream=nullptr;
    this->audio_device=0;
    this->keepRunning=false;
}
AudioOutput::~AudioOutput()
{
    close();
}

bool AudioOutput::init()
{
    SDL_AudioSpec spec;
    spec.format=SDL_AUDIO_F32;
    spec.channels =2;
    spec.freq=44100;
    this->audio_device=SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
    if(audio_device==0)
    {
        this->close();
        return false;
    }
    audio_stream = SDL_CreateAudioStream(&spec, &spec);//快速建立音频通道,先使用默认参数初始化
    if(!audio_stream)
    {
        this->close();
        return false;
    }
    SDL_BindAudioStream(this->audio_device, this->audio_stream);
    return true;
}
void AudioOutput::start()
{
    if(keepRunning)return;
    keepRunning=true;
    feed_audio_thread = std::thread(&AudioOutput::feed_audio_loop,this);
}
void AudioOutput::stop()
{
    keepRunning=false;
    if(feed_audio_thread.joinable())
    {
        feed_audio_thread.join();
    }
}
void AudioOutput::close()
{
    stop();
    if(audio_stream)
    {
        SDL_DestroyAudioStream(audio_stream);
        audio_stream=nullptr;
    }
    if(audio_device)
    {
        SDL_CloseAudioDevice(audio_device);
        audio_device=0;
    }
}
void AudioOutput::feed_audio_loop()
{
    while(keepRunning)
    {
        if(!audio_stream)break;
        if(SDL_GetAudioStreamQueued(audio_stream)>32768)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        if(state->audio_frame_queue.size()<=0)
        {
               std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        AVFrame *frame=state->audio_frame_queue.pop();
        if(!frame)continue;
        if(frame->pts!=AV_NOPTS_VALUE)
        {
            double pts=frame->pts*av_q2d(state->audio_time_base);
            state->audio_clock.set_clock(pts);
        }
        int data_size = av_samples_get_buffer_size(nullptr, frame->ch_layout.nb_channels, frame->nb_samples,  (AVSampleFormat)frame->format, 1);
        if(data_size>0)
        {
            SDL_PutAudioStreamData(audio_stream, frame->data[0], data_size);
        }
        av_frame_free(&frame);
    }
}
double AudioOutput::get_audio_delay()
{
    int queued_bytes=SDL_GetAudioStreamQueued(audio_stream);
    return (double)queued_bytes/352800.0;
}
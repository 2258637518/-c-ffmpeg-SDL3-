#include <SDL3/SDL_events.h>
#include <iostream>
#include <SDL3/SDL.h>
#include "media_state.h"
#include "demuxer.h"
#include "video_decoder.h"
#include "audio_decoder.h"
#include "renderer.h"

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    const std::string filepath = "/home/gloomybunny/video/GTA6_Trailer2.mp4";
    MediaState media_state(filepath);
    Demuxer demuxer(&media_state);
    Renderer renderer(&media_state);

    demuxer.open();
    demuxer.print_metadata();

    AudioDecoder audio_decoder(&media_state,media_state.audio_stream_idx);
    VideoDecoder video_decoder(&media_state,media_state.video_stream_idx);
    
    audio_decoder.open();
    video_decoder.open();
    renderer.init();
    demuxer.start();
    audio_decoder.start();
    video_decoder.start();
    renderer.render_audio();
    while(!renderer.should_quit())
    {
        renderer.render_video();
<<<<<<< HEAD
        SDL_Delay(2);
=======
>>>>>>> 65e2bd8 (独立音频解码线程,音视频同步效果改进)
    }
    video_decoder.stop();
    audio_decoder.stop();
    demuxer.stop();
    renderer.close();
    return 0;
}

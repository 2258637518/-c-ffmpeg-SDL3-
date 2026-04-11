#pragma once
#include "decoder.h"
#include "audio_filter.h"
extern "C" {
#include <libswresample/swresample.h>
}
class AudioDecoder : public Decoder {
public:
    AudioDecoder(MediaState *state, int stream_idx);
    ~AudioDecoder();
    bool open();
protected:
    void decode_thread_loop() override;

private:
void resample_and_push(AVFrame* in_frame);

SwrContext *swr_ctx;
AudioFilter filter;
    double current_source_sec = -1.0;
};

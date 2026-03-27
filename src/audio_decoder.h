#pragma once
#include "decoder.h"
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
SwrContext *swr_ctx;
};

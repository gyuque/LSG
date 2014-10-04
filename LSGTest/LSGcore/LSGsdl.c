#include <SDL.h>
#include "LSGsdl.h"
#define LSGSDL_VERBOSE 1

static void sFillAudioBufferCallback(void* userdata, Uint8* stream, int len);

int lsg_sdl_start() {
    lsg_initialize();


    SDL_AudioSpec desired, actualSpec;
    desired.freq = kLSGOutSamplingRate;
    desired.format = AUDIO_S16MSB;
    desired.channels = 2;
    desired.samples = 2048;
    desired.callback = &sFillAudioBufferCallback;
    desired.userdata = NULL;
    
    const int sdlrv = SDL_OpenAudio(&desired, &actualSpec);
    if (sdlrv < 0) {
        return sdlrv;
    }
    
    if (desired.format != actualSpec.format) {
        fputs("*** Warning! buffer format changed ***\n", stderr);
    }
    
#if LSGSDL_VERBOSE
    fputs("Opened SDL Audio\n", stderr);
    fprintf(stderr, "Output sampling rate = %d\n", actualSpec.freq);
#endif
    
    SDL_PauseAudio(0);
    return 0;
}

void sFillAudioBufferCallback(void* userdata, Uint8* stream, int len) {
    // ***WARNING*** Here is NOT main thread.
    const int nSamples = len / 4;

    lsg_synthesize_BE16(stream, nSamples, 4, 1);
}
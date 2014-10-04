#include <SDL.h>
#include "LSGsdl.h"
#define LSGSDL_VERBOSE 1

static void sFillAudioBufferCallback(void* userdata, Uint8* stream, int len);
static int sActualSampleFormat = AUDIO_S16MSB;
static int sSDLBufferGo = 0;

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
    
    sActualSampleFormat = actualSpec.format;
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
    if (!sSDLBufferGo) {
        return;
    }

    // ***WARNING*** Here is NOT main thread.
    const int nSamples = len / 4;

    if (sActualSampleFormat == AUDIO_S16MSB) {
        lsg_synthesize_BE16(stream, nSamples, 4, 1);
    } else {
        lsg_synthesize_LE16(stream, nSamples, 4, 1);
    }
}

void lsg_sdl_set_running(int b) {
    sSDLBufferGo = b;
}
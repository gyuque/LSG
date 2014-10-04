#include <stdio.h>
#include <SDL.h>
#include <string>
#include "MusicPreset.h"
#include "../../LSGTest/LSGcore/LSGsdl.h"

#define kNRsvBufs 8

static bool lookupInputName(std::string& outStr, int argc, char* argv[]);
static void configureLSG(const MusicPreset& preset);
static void loadMidi(const MusicPreset& preset);
static void setupReserveBuffers();
static void destroyReserveBuffers();
static void bindReserveBuffers();

LSGReservedCommandBuffer_t sRsvbufs[kNRsvBufs];
MLFPlaySetup_t sMLFSetup;


int main(int argc, char * argv[])
{
    std::string presetFilename;
    if (!lookupInputName(presetFilename, argc, argv)) {
        fputs("Specify mapping file.\n", stderr);
        return 0;
    }
    
    MusicPreset preset;
    if (!preset.loadFromYAMLFile(presetFilename.c_str())) {
        fputs("Failed to load mapping file.\n", stderr);
        return -1;
    }
    
    preset.dump();
    fputs("\n\n", stderr);
    SDL_Delay(250);
    
    fprintf(stderr, "LSG ONGEN (SDL backend) test\n");
    fprintf(stderr, "----------------------------\n");
    SDL_Init(SDL_INIT_AUDIO);
    
    lsg_mlf_init_play_setup_struct(&sMLFSetup);
    setupReserveBuffers();
    loadMidi(preset);

    lsg_sdl_start();
    lsg_rsvcmd_fill_mlf(sRsvbufs, kNRsvBufs, &sMLFSetup, 8820);
    bindReserveBuffers();
    configureLSG(preset);
    
/*
    const int ch = 0;
    ChannelCommand testcmd = kLSGCommandBit_KeyOn | kLSGCommandBit_Enable | 72;
    lsg_put_channel_command_and_clear_later(ch, 0, testcmd);
    
    ChannelCommand testcmd2 = kLSGCommandBit_KeyOn | kLSGCommandBit_Enable | 84;
    lsg_put_channel_command_and_clear_later(ch, 44, testcmd2);
    
    ChannelCommand testcmd3 = kLSGCommandBit_Enable;
    lsg_put_channel_command_and_clear_later(ch, 88, testcmd3);
*/
    
    lsg_sdl_set_running(1);
    getchar();
    SDL_Quit();
    destroyReserveBuffers();
    lsg_mlf_destroy_play_setup_struct(&sMLFSetup);
    return 0;
}

bool lookupInputName(std::string& outStr, int argc, char* argv[]) {
    if (argc < 2) {
        return false;
    }
    
    outStr = argv[1];
    return true;
}

void configureLSG(const MusicPreset& preset) {

    for (int ch = 0;ch < kNRsvBufs;++ch) {
        if (!preset.isChannelMapped(ch)) {
            continue;
        }
        
        // Generator setup
        const MappedChannelConf& chconf = preset.getChannelConf(ch);
        switch (chconf.generatorType) {
            case G_TRIANGLE:
                lsg_generate_triangle(ch);
                break;

            case G_NOISE:
                lsg_generate_short_noise(ch);
                break;

            case G_SQUARE13:
                lsg_generate_square_13(ch);
                break;
                
            case G_IFT: {
                const float* coefs = &chconf.coefficients[0];
                lsg_generate_sin_v(ch, coefs, chconf.coefficients.size());
            } break;
                
            default:
                lsg_generate_square(ch);
                break;
        }
        lsg_set_channel_source_generator(ch, ch);
        
        
        lsg_set_channel_global_detune(ch, chconf.detune);
        lsg_set_channel_global_volume(ch, (float)kLSGChannelVolumeMax * chconf.volume);
    }
}

void setupReserveBuffers() {
    for (int i = 0;i < kNRsvBufs;++i) {
        lsg_rsvcmd_init(&sRsvbufs[i], 32768);
    }
}

void bindReserveBuffers() {
    for (int i = 0;i < kNRsvBufs;++i) {
        if (sRsvbufs[i].length > 0) {
            lsg_channel_bind_rsvcmd(i, &sRsvbufs[i]);
        }
    }
}

void loadMidi(const MusicPreset& preset) {
    lsg_mlf_init_channel_mapping(sMLFSetup.chmap, kLSGNumOutChannels);
    sMLFSetup.deltaScale = 150;
    
    fprintf(stderr, "- - Loading sequence... - -\n");
    lsg_mlf_t mlf;
    lsg_load_mlf(&mlf, preset.getInputName());
    fprintf(stderr, "Tempo=%d  Timebase=%d\n", mlf.tempo, mlf.timeBase);
    
    for (int i = 0;i < kNRsvBufs;++i) {
        if (preset.isChannelMapped(i)) {
            const MappedChannelConf& chconf = preset.getChannelConf(i);
            sMLFSetup.chmap[i].sortedEvents = lsg_mlf_create_sorted_channel_events(&mlf, chconf.midiCh);
            sMLFSetup.chmap[i].eventsLength = lsg_mlf_count_channel_events(&mlf, chconf.midiCh);
            sMLFSetup.chmap[i].defaultADSR = chconf.adsr;
        }
    }
    
    sMLFSetup.deltaScale = lsg_util_calc_delta_time_scale(&mlf);
    
    lsg_free_mlf(&mlf);
}

void destroyReserveBuffers() {
    for (int i = 0;i < kNRsvBufs;++i) {
        lsg_rsvcmd_destroy(&sRsvbufs[i]);
    }
}

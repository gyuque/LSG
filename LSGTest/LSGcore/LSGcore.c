#include <stdio.h>
#define _USE_MATH_DEFINES
#include <math.h>
#include <memory.h>
#include "LSG.h"
#define generator_index_in_range(x) ((x) >= 0 && (x) < kLSGNumGenerators)
#define generator_index_good(x) (((x) >= 0 && (x) < kLSGNumGenerators) || (x) == kLSGWhiteNoiseGeneratorSpecialIndex)
#define channel_index_in_range(x) ((x) >= 0 && (x) < kLSGNumOutChannels)

#ifdef __arm__
#define LSGDEBUG_VERBOSE_COMMAND 0
#else
#define LSGDEBUG_VERBOSE_COMMAND 1
#endif

static char sLSGBufferRunning = 1;

static const float sNoteTable[12] = {
    32.703196f, // 0  C
    34.647829f, // 1   C+
    36.708096f, // 2  D
    38.890873f, // 3   D+
    
    41.203445f, // 4  E
    43.653529f, // 5  F
    46.249303f, // 6   F+
    48.999429f, // 7  G
    
    51.913087f, // 8   G+
    55.0f     , // 9  A
    58.270470f, // 10  A+
    61.735413f  // 11 B
};

static const int sSemitoneFlagTable[12] = {
    0, // 0  C
    1, // 1   C+
    0, // 2  D
    1, // 3   D+
    
    0, // 4  E
    0, // 5  F
    1, // 6   F+
    0, // 7  G
    
    1, // 8   G+
    0, // 9  A
    1, // 10  A+
    0  // 11 B
};

static const float sFIRTable17[17] = {
    0.0f,
    0.000068f,
    0.001881f,
    0.001395f,
   -0.021226f,
   -0.036988f,
    0.063382f,
    0.283174f,
    0.417058f,
    0.283174f,
    0.063382f,
   -0.036988f,
   -0.021226f,
    0.001395f,
    0.001881f,
    0.000068f,
    0.0f
};

static const float sFIRTable31[31] ={ 0, -0.000003682586018518, -0.000088028839469061, -0.000029040779732058, 0.0010784227814733, 0.0014687531436647,
-0.0034569422826696, -0.0083635434148992, 0.0032702997919358, 0.024625314361636, 0.011093218999269, -0.04856620452975,
-0.063476786067698, 0.070943134160791, 0.29907779451732, 0.42483870967742, 0.29907779451732, 0.070943134160791,
-0.063476786067698, -0.04856620452975, 0.011093218999269, 0.024625314361636, 0.0032702997919358, -0.0083635434148992,
-0.0034569422826696, 0.0014687531436647, 0.0010784227814733,
    -0.000029040779732058, -0.00008802883946906, -0.000003682586018518, 0};

static const float sFIRTable31_2[31] = {0, -0.000003461629530124, 0.000021458207524647, 0.00031673735569006, 0.001138648914712, 0.0018785024735357, 0.000461009277752,
    -0.005431234657147, -0.015124521783638, -0.02205292618621, -0.014684763138026, 0.017321837795055, 0.074695443394287, 0.14391357642232, 0.20104773711845, 0.23322580645161, 0.20104773711845,
    0.14391357642232, 0.074695443394287, 0.017321837795055, -0.014684763138026, -0.02205292618621, -0.015124521783638, -0.005431234657147, 0.000461009277752, 0.0018785024735357, 0.001138648914712,
    0.00031673735569006, 0.000021458207524647, -0.000003461629530124, 0};

static const float sFIRTable63[63] = {0, -0, -0.000001, -0.000003, 0.000001, 0.000028, 0.0001, 0.000239, 0.000448, 0.0007, 0.000916, 0.000972, 0.000708, -0.000037, -0.001373,
    -0.003306, -0.005679, -0.008146, -0.01017, -0.011066, -0.010083, -0.006526, 0.000115, 0.010038, 0.023033, 0.03845,
    0.055222, 0.071969, 0.087157, 0.099294, 0.107132, 0.119841, 0.107132, 0.099294, 0.087157, 0.071969, 0.055222, 0.03845,
    0.023033, 0.010038, 0.000115, -0.006526, -0.010083, -0.011066, -0.01017, -0.008146, -0.005679, -0.003306, -0.001373, -0.000037, 0.000708, 0.000972, 0.000916, 0.0007,
    0.000448, 0.000239, 0.0001, 0.000028, 0.000001, -0.000003, -0.000001, -0, 0};

static float sCustomNoteMapping[kLSGNoteMappingLength];

#define kBinNoiseFeedback 0x4000
#define kBinNoiseTap1     0x01
#define kBinNoiseTap2     0x02

static int64_t sGlobalTick = 0;
static LSGSample sGeneratorBuffers[kLSGNumGenerators][kLSGNumGeneratorSamples];
static LSGSample sGeneratorTempBuf[kLSGNumGeneratorSamples];
static LSGChannel_t sChannelStatuses[kLSGNumOutChannels];

static LSGStatus lsg_initialize_channel(LSGChannel_t* ch);
static LSGStatus lsg_initialize_channel_command_buffer(LSGChannel_t* ch);
static LSGStatus lsg_initialize_channel_fir_buffer(LSGChannel_t* ch);
static LSGStatus lsg_initialize_generators();
static LSGStatus lsg_fill_generator_buffer(LSGSample* buf, size_t len, LSGSample val);
static LSGStatus lsg_apply_channel_command(LSGChannel_t* ch, ChannelCommand cmd, int commandOffsetPosition);
static LSGSample lsg_calc_channel_gain(LSGChannel_t* ch);
static LSGSample lsg_update_channel_fir(LSGChannel_t* ch, LSGSample newValue);
static LSGStatus lsg_fill_reserved_commands(int64_t startTick, LSGChannel_t* ch);
static LSGStatus lsg_apply_generator_filter(int generatorIndex);
static LSGStatus lsg_apply_channel_system_fade(LSGChannel_t* ch);

LSGStatus lsg_initialize() {
    sGlobalTick = 0;
    sLSGBufferRunning = 1;

    if (lsg_initialize_generators() != LSG_OK) {
        return LSGERR_GENERIC;
    }
    
    lsg_initialize_custom_note_table();
    
    for (int i = 0;i < kLSGNumOutChannels;++i) {
        lsg_initialize_channel(&sChannelStatuses[i]);
        lsg_channel_initialize_volume_params(i);
        sChannelStatuses[i].selfIndex = i;
    }
    
    return LSG_OK;
}

int64_t lsg_get_global_tick() {
    return sGlobalTick;
}

LSGStatus lsg_set_buffer_running(char bRunning) {
    sLSGBufferRunning = bRunning;
    return LSG_OK;
}

int lsg_get_semitone_flag(int noteIndex) {
    return sSemitoneFlagTable[noteIndex % 12];
}

void lsg_set_force_global_tick(int64_t t) {
    sGlobalTick = t;
}

LSGStatus lsg_initialize_generators() {
    int i;
    
    for (i = 0;i < kLSGNumGenerators;++i) {
        lsg_fill_generator_buffer(sGeneratorBuffers[i], kLSGNumGeneratorSamples, 0);
    }
    
    return LSG_OK;
}

LSGStatus lsg_channel_initialize_volume_params(int channelIndex) {
    if (!channel_index_in_range(channelIndex)) {
        return LSGERR_PARAM_OUTBOUND;
    }
    
    LSGChannel_t* ch = &sChannelStatuses[channelIndex];
    ch->volume = kLSGChannelVolumeMax;
    ch->global_volume = kLSGChannelVolumeMax;
    ch->system_volume = ch->system_vol_dest = kLSGChannelVolumeMax;

    return LSG_OK;
}

LSGStatus lsg_initialize_channel(LSGChannel_t* ch) {
    ch->fq = ch->bent_fq = 440;
    ch->lastNote = 0;
    ch->global_detune = 0;
    
//    ch->fq = 261.625565f;
//    ch->fq = 293.66476f;
    ch->generatorIndex = 0;
    ch->customNoteIndex = 0;
    ch->currentBaseGain4X = 0;
    ch->readPos = 0;
    
    ch->adsr.attack_rate = kLSGRawGainMax4X >> 4;
    ch->adsr.decay_rate = 8;
    ch->adsr.sustain_level = kLSGRawGainMax4X >> 2;
    ch->adsr.release_rate = 2;
    ch->adsr.fade_rate = 0;
    
    ch->keyonCount = -1;
    ch->adsrPhase = 0;
    ch->noiseRegister = kBinNoiseFeedback;
    
    ch->pReservedCommandBuffer = NULL;
    
    ch->exec_callback = NULL;
    ch->userDataForCallback = NULL;
    
    lsg_initialize_channel_command_buffer(ch);
    lsg_initialize_channel_fir_buffer(ch);

    return LSG_OK;
}

LSGStatus lsg_initialize_channel_keyon(int channelIndex) {
    if (!channel_index_in_range(channelIndex)) {
        return LSGERR_PARAM_OUTBOUND;
    }
    
    sChannelStatuses[channelIndex].currentBaseGain4X = 0;
    sChannelStatuses[channelIndex].keyonCount = -1;
    return LSG_OK;
}


LSGStatus lsg_initialize_custom_note_table() {
    for (int i = 0;i < kLSGNoteMappingLength;++i) {
        sCustomNoteMapping[i] = 440.0f;
    }
    
    return LSG_OK;
}

LSGStatus lsg_set_custom_note_frequency(int index, float fq) {
    if (index >= kLSGNoteMappingLength) { return LSGERR_PARAM_OUTBOUND; }
    if (index < 0) { index = 0; }
    
    sCustomNoteMapping[index] = fq;
        
    return LSG_OK;
}

LSGStatus lsg_use_custom_notes(int channelIndex, int customNotesIndex) {
    if (!channel_index_in_range(channelIndex)) {
        return LSGERR_PARAM_OUTBOUND;
    }
    
    sChannelStatuses[channelIndex].customNoteIndex = customNotesIndex;
    return LSG_OK;
}

LSGStatus lsg_initialize_channel_command_buffer(LSGChannel_t* ch) {
    for (int i = 0;i < kChannelCommandBufferLength;++i) {
        ch->commandRingBuffer[i] = 0;
    }
    
    ch->ringHeadPos = 0;
    
    return LSG_OK;
}

LSGStatus lsg_initialize_channel_fir_buffer(LSGChannel_t* ch) {
    LSGSample* buf = ch->fir_buf;
    
    for (int i = 0;i < kChannelFIRLength;++i) {
        buf[i] = 0;
    }
    
    return LSG_OK;
}

ChannelCommand lsg_consume_channel_command_buffer(LSGChannel_t* ch) {
    // read
    const ChannelCommand cmd = ch->commandRingBuffer[ ch->ringHeadPos ];

    // clear
    ch->commandRingBuffer[ ch->ringHeadPos ] = 0;
    
    // advance
    ch->ringHeadPos = (ch->ringHeadPos + 1) % kChannelCommandBufferLength;

    return cmd;
}

LSGStatus lsg_set_channel_source_generator(int channelIndex, int generatorBufferIndex) {
    if (!channel_index_in_range(channelIndex) || !generator_index_in_range( generatorBufferIndex )) {
        return LSGERR_PARAM_OUTBOUND;
    }
    
    sChannelStatuses[channelIndex].generatorIndex = generatorBufferIndex;
    
    return LSG_OK;
}

LSGStatus lsg_set_channel_white_noise(int channelIndex) {
    if (!channel_index_in_range(channelIndex)) {
        return LSGERR_PARAM_OUTBOUND;
    }
    
    sChannelStatuses[channelIndex].generatorIndex = kLSGWhiteNoiseGeneratorSpecialIndex;

    return LSG_OK;
}

LSGStatus lsg_set_channel_adsr(int channelIndex, LSG_ADSR* pSourceADSR) {
    if (!channel_index_in_range(channelIndex) || !pSourceADSR) {
        return LSGERR_PARAM_OUTBOUND;
    }
    
    sChannelStatuses[channelIndex].adsr = *pSourceADSR;
    
    return LSG_OK;
}

LSGStatus lsg_get_channel_adsr(int channelIndex, LSG_ADSR* pOutADSR) {
    if (!channel_index_in_range(channelIndex) || !pOutADSR) {
        return LSGERR_PARAM_OUTBOUND;
    }
    
    *pOutADSR = sChannelStatuses[channelIndex].adsr;
    
    return LSG_OK;
}

LSGStatus lsg_get_channel_copy(int channelIndex, LSGChannel_t* pOut) {
    if (!channel_index_in_range(channelIndex) || !pOut) {
        return LSGERR_PARAM_OUTBOUND;
    }
    
    *pOut = sChannelStatuses[channelIndex];
    
    return LSG_OK;
}

LSGStatus lsg_noteoff_channel_immediately(int channelIndex) {
    if (!channel_index_in_range(channelIndex)) {
        return LSGERR_PARAM_OUTBOUND;
    }
    
    LSGChannel_t* ch = &sChannelStatuses[channelIndex];
    ch->keyonCount = -1;
    ch->currentBaseGain4X = 0;
    
    return LSG_OK;
}

LSGStatus lsg_set_channel_command_exec_callback(int channelIndex, lsg_channel_command_executed_callback callback, void* userData) {
    if (!channel_index_in_range(channelIndex)) {
        return LSGERR_PARAM_OUTBOUND;
    }
    
    sChannelStatuses[channelIndex].exec_callback = callback;
    sChannelStatuses[channelIndex].userDataForCallback = userData;
    
    return LSG_OK;
}

LSGStatus lsg_put_channel_command_and_clear_later_internal(LSGChannel_t* ch, int offset, ChannelCommand cmd) {
    ChannelCommand* buf = ch->commandRingBuffer;
    const int filllen = kChannelCommandBufferLength - offset;
    for (int i = 0;i < filllen;++i) {
        const int bufPos = (ch->ringHeadPos + offset + i) % kChannelCommandBufferLength;
        if (i == 0) {
            buf[bufPos] = cmd;
        } else {
            buf[bufPos] = 0;
        }
    }
    
    return LSG_OK;
}

LSGStatus lsg_put_channel_command_and_clear_later(int channelIndex, int offset, ChannelCommand cmd) {
    if (channelIndex < 0 || channelIndex >= kLSGNumOutChannels) {
        return LSGERR_PARAM_OUTBOUND;
    }
    
    LSGChannel_t* ch = &sChannelStatuses[channelIndex];
    return lsg_put_channel_command_and_clear_later_internal(ch, offset, cmd);
}

static LSG_INLINE float calcNoteFreq(int noteNo, int custom) {
    if (custom) {
        return sCustomNoteMapping[noteNo];
    }

    const float mul = (noteNo == 1) ? 0.025f : 0.25f;
    const int oct  = noteNo / 12;
    const int nidx = noteNo % 12;
    
    return sNoteTable[nidx] * powf(2, oct) * mul;
}

LSGStatus lsg_apply_channel_command(LSGChannel_t* ch, ChannelCommand cmd, int commandOffsetPosition) {
    if ((cmd & kLSGCommandBit_Enable) == 0) {
        return LSG_OK;
    }
    
    if ((cmd & kLSGCommandBit_NoKey) == 0) {
        const int keyon = cmd & kLSGCommandBit_KeyOn;
        if (!keyon) {
            ch->keyonCount = -1;
        } else {
            ch->keyonCount = 0;
            ch->adsrPhase = 0;
        }
    }
    // else modify params only
    
    
    if (cmd & kLSGCommandBit_Volume) {
        ch->volume = (cmd & kLSGCommandMask_Volume) >> 16;
    }
    
    const int noteNo = cmd & kLSGCommandMask_NoteNum;
    if (noteNo) {
        float base_fq = calcNoteFreq(noteNo, ch->customNoteIndex);
        
        ch->fq = ch->bent_fq = base_fq;
        ch->lastNote = noteNo;
    }

    const uint32_t pitchbits = cmd & (kLSGCommandMask_Pitch | kLSGCommandMask_PitchParam);
    if (pitchbits) {
        const int is_up = pitchbits & kLSGCommandBit_PitchUp;
        const int pitch_amount = (pitchbits >> 8) & 0x3f;
        const float pfq = calcNoteFreq(ch->lastNote + (is_up ? 2 : -2), ch->customNoteIndex);
        ch->bent_fq = ch->fq + (pfq - ch->fq) * (float)pitch_amount / 63.0f;
    }

    if (ch->exec_callback) {
        ch->exec_callback(ch->userDataForCallback, ch->selfIndex, cmd, commandOffsetPosition);
    }
    
    return LSG_OK;
}

LSGStatus lsg_apply_channel_adsr(LSGChannel_t* ch) {
    switch(ch->adsrPhase) {
        // Attack
        case 0:
        if (ch->keyonCount < 0) {
            ch->adsrPhase = 2;
            break;
        }
        
        ch->currentBaseGain4X += ch->adsr.attack_rate;
        if (ch->currentBaseGain4X > kLSGRawGainMax4X) {
            ch->currentBaseGain4X = kLSGRawGainMax4X;
            ++ch->adsrPhase;
        }
        break;
        
        // Decay
        case 1:
        if (ch->keyonCount < 0) {
            ch->adsrPhase = 2;
            break;
        }
        
        ch->currentBaseGain4X -= ch->adsr.decay_rate;
        if (ch->currentBaseGain4X < ch->adsr.sustain_level) {
            ch->currentBaseGain4X = ch->adsr.sustain_level;
            ++ch->adsrPhase;
            ch->keyonCount = 1;
        }
        break;
        
        // Sustain and Release
        default:
        if (ch->keyonCount >= 0) {
            ch->currentBaseGain4X = ch->adsr.sustain_level - ch->keyonCount * ch->adsr.fade_rate;
            if (ch->currentBaseGain4X < 0) {
                ch->currentBaseGain4X = 4;
                ch->keyonCount = -1;
            }
        } else if (ch->currentBaseGain4X > 0) {
            if (ch->currentBaseGain4X > ch->adsr.sustain_level) {
                ch->currentBaseGain4X -= ch->adsr.decay_rate;
            } else {
                ch->currentBaseGain4X -= ch->adsr.release_rate;
            }
            
            if (ch->currentBaseGain4X < 0) {
                ch->currentBaseGain4X = 0;
            }
        }
        break;
    }
    
    return LSG_OK;
}

LSGStatus lsg_advance_channel_state(LSGChannel_t* ch) {
    
    if (ch->keyonCount >= 0) {
        ++ch->keyonCount;
    }
    
    return LSG_OK;
}

static LSG_INLINE int lsg_channel_noise_next(LSGChannel_t* ch) {
    if (((ch->noiseRegister & kBinNoiseTap1) != 0) != ((ch->noiseRegister & kBinNoiseTap2) != 0)) {
        ch->noiseRegister = (ch->noiseRegister >> 1) | kBinNoiseFeedback;
    } else {
        ch->noiseRegister >>= 1;
    }
    
    return ((ch->noiseRegister & 1) << 15) - 16384;
}

/*
static inline int lsg_channel_short_noise_next(LSGChannel_t* ch) {
    if ((ch->noiseRegister & kBinNoiseTap1) != 0) { // Use only one tap
        ch->noiseRegister = (ch->noiseRegister >> 1) | kBinNoiseFeedback;
    } else {
        ch->noiseRegister >>= 1;
    }
    
    return ((ch->noiseRegister & 1) << 15) - 16384;
}*/

LSGSample lsg_calc_channel_gain(LSGChannel_t* ch) {
    int generatorValue = 0;
    if (ch->generatorIndex == kLSGWhiteNoiseGeneratorSpecialIndex) {
        generatorValue = lsg_channel_noise_next(ch);
    } else {
        generatorValue = sGeneratorBuffers[ch->generatorIndex][ch->readPos];
    }
    
    const int beforeVolume = ((ch->currentBaseGain4X >> 2) * generatorValue) / (kLSGRawGainMax4X >> 2);
    
    return beforeVolume;
}

LSGStatus lsg_set_channel_frequency(int channelIndex, float fq) {
    if (channelIndex < 0 || channelIndex >= kLSGNumOutChannels) {
        return LSGERR_PARAM_OUTBOUND;
    }
    
    sChannelStatuses[channelIndex].fq = fq;
    
    return LSG_OK;
}

LSGStatus lsg_set_channel_global_detune(int channelIndex, float d) {
    if (channelIndex < 0 || channelIndex >= kLSGNumOutChannels) {
        return LSGERR_PARAM_OUTBOUND;
    }
    
    sChannelStatuses[channelIndex].global_detune = d;
    fprintf(stderr, "DETUNE: %f\n", sChannelStatuses[channelIndex].global_detune);
    return LSG_OK;
}

LSGStatus lsg_set_channel_global_volume(int channelIndex, int v) {
    if (channelIndex < 0 || channelIndex >= kLSGNumOutChannels) {
        return LSGERR_PARAM_OUTBOUND;
    }
    
    if (v < 0) {v = 0;}
    else if (v > kLSGChannelVolumeMax) { v = kLSGChannelVolumeMax; }
    
    sChannelStatuses[channelIndex].global_volume = v;
    
    return LSG_OK;
}

LSGStatus lsg_set_channel_system_volume(int channelIndex, int vol) {
    if (channelIndex < 0 || channelIndex >= kLSGNumOutChannels) {
        return LSGERR_PARAM_OUTBOUND;
    }
    
    if (vol < 0) {vol = 0;}
    else if (vol > kLSGChannelVolumeMax) { vol = kLSGChannelVolumeMax; }
    
    sChannelStatuses[channelIndex].system_volume = vol;
    
    return LSG_OK;
}

LSGStatus lsg_set_channel_auto_fade(int channelIndex, int dest_vol) {
    if (channelIndex < 0 || channelIndex >= kLSGNumOutChannels) {
        return LSGERR_PARAM_OUTBOUND;
    }

    sChannelStatuses[channelIndex].system_vol_dest = dest_vol;
    return LSG_OK;
}

LSGStatus lsg_set_channel_auto_fade_max(int channelIndex) {
    return lsg_set_channel_auto_fade(channelIndex, kLSGChannelVolumeMax);
}

LSGStatus lsg_fill_generator_buffer(LSGSample* buf, size_t len, LSGSample val) {
    size_t i;
    
    for (i = 0;i < len;++i) {
        buf[i] = val;
    }
    
    return LSG_OK;
}

LSGSample lsg_update_channel_fir(LSGChannel_t* ch, LSGSample newValue) {
    LSGSample* buf = ch->fir_buf;
    buf[8] = buf[7];
    buf[7] = buf[6];
    buf[6] = buf[5];
    buf[5] = buf[4];
    buf[4] = buf[3];
    buf[3] = buf[2];
    buf[2] = buf[1];
    buf[1] = buf[0];
    buf[0] = newValue;

    return (float)buf[1] * 0.0039866f +
           (float)buf[2] *-0.0495225f +
           (float)buf[3] * 0.1564982f +
           (float)buf[4] * 0.7788888f +
           (float)buf[5] * 0.7788888f +
           (float)buf[6] *-0.0495225f +
           (float)buf[7] * 0.0039866f;

/*
    return (float)buf[1] * 0.0078283f +
           (float)buf[2] * 0.1932567f +
           (float)buf[3] * 0.4328571f +
           (float)buf[4] * 0.1932567f +
           (float)buf[5] * 0.0078283f;*/
}

LSGStatus lsg_apply_channel_system_fade(LSGChannel_t* ch) {
    if (ch->system_vol_dest == ch->system_volume) {
        return LSG_OK;
    }
    
    if (ch->system_volume < ch->system_vol_dest) {
        ch->system_volume += 1;
        if (ch->system_volume > ch->system_vol_dest) { ch->system_volume = ch->system_vol_dest; }
    } else {
        ch->system_volume -= 1;
        if (ch->system_volume < ch->system_vol_dest) { ch->system_volume = ch->system_vol_dest; }
    }
    
    return LSG_OK;
}

// ==== OUTPUT API ====
static LSG_INLINE LSGStatus lsg_synthesize_internal(unsigned char* pOut, size_t nSamples, int strideBytes, const int bStereo, int bLE) {
    const float baseFQ = (float)kLSGOutSamplingRate / (float)kLSGNumGeneratorSamples;
    int writePos = 0;
    int ci;
    
    const int Hi = bLE ? 1 : 0;
    const int Lo = bLE ? 0 : 1;
    
    // Fill (if reserved)
    for (ci = 0;ci < kLSGNumOutChannels;++ci) {
        LSGChannel_t* ch = &sChannelStatuses[ci];
        lsg_fill_reserved_commands(sGlobalTick, ch);
    }
    
    const int vmax2 = kLSGChannelVolumeMax * kLSGChannelVolumeMax;
    for (int i = 0;i < nSamples;++i) {
        int val = 0;
        if (sLSGBufferRunning) {

            int should_fetch_command = ((sGlobalTick % (uint64_t)kChannelCommandInterval) == 0);
            for (ci = 0;ci < kLSGNumOutChannels;++ci) {
                LSGChannel_t* ch = &sChannelStatuses[ci];
                if (should_fetch_command) {
                    const ChannelCommand cmd = lsg_consume_channel_command_buffer(ch);
    if ((cmd & kLSGCommandBit_Enable) && LSGDEBUG_VERBOSE_COMMAND)
    fprintf(stderr, "Ch: %2d   CMD: %x   t:%8lld\n", ci, cmd, sGlobalTick);
                    lsg_apply_channel_command(ch, cmd, i);
                    lsg_apply_channel_system_fade(ch);
                }
                
                lsg_apply_channel_adsr(ch);
                lsg_advance_channel_state(ch);

                const int fstep = (ch->bent_fq + ch->global_detune) / baseFQ;
                ch->readPos = (ch->readPos + fstep) % kLSGNumGeneratorSamples;
                const int channelVal = (lsg_calc_channel_gain(ch) * ch->volume * ch->global_volume) / vmax2;
    //            val += lsg_update_channel_fir(ch, channelVal);
                val += (channelVal * ch->system_volume) / kLSGChannelVolumeMax;
            }

            if (val > 32767) { val = 32767; }
            else if (val < -32767) { val = -32767; }
            
            ++sGlobalTick;
        }

        // Write   - - - - - - - - - - - - - - -
        pOut[writePos+Hi] = (val & 0xff00) >> 8;
        pOut[writePos+Lo] =  val & 0xff;
        if (bStereo) {
            pOut[writePos+2+Hi] = (val & 0xff00) >> 8;
            pOut[writePos+2+Lo] =  val & 0xff;
        }
        
        writePos += strideBytes;
    }
    
    return LSG_OK;
}

LSGStatus lsg_synthesize_BE16(unsigned char* pOut, size_t nSamples, int strideBytes, const int bStereo) {
    return lsg_synthesize_internal(pOut, nSamples, strideBytes, bStereo, 0);
}

LSGStatus lsg_synthesize_LE16(unsigned char* pOut, size_t nSamples, int strideBytes, const int bStereo) {
    return lsg_synthesize_internal(pOut, nSamples, strideBytes, bStereo, 1);
}

LSGStatus lsg_fill_reserved_commands(int64_t startTick, LSGChannel_t* ch) {
    const int peek_max = 441;
    if (!ch) {
        return LSGERR_NULLPTR;
    }
    
    if (!ch->pReservedCommandBuffer) {
        return LSG_OK;
    }
    
    LSGReservedCommandBuffer_t* rb = ch->pReservedCommandBuffer;
    const int64_t endTick = startTick + (kChannelCommandBufferLength * kChannelCommandInterval);
    int64_t tOffset = 0;
    int64_t tSpanInLoop = rb->loopEndTime - rb->loopStartTime;
    
    const int use_loop = (rb->loopLastIndex > rb->loopFirstIndex);
    const int n_in_loop = (int)(rb->loopLastIndex - rb->loopFirstIndex) + 1;
    
    for (int i = 0;i < peek_max;++i) {
        tOffset = 0;
        int rv_index = rb->readPosition;
        
        // Make looped index and time offset
        if (use_loop) {
            if (rv_index >= rb->loopFirstIndex) {
                const int i_from_loopstart = rv_index - (int)rb->loopFirstIndex;
                const int li = i_from_loopstart % n_in_loop;
                const int64_t loopCount = i_from_loopstart / n_in_loop;
                rv_index = (int)rb->loopFirstIndex + li;
                tOffset = loopCount * tSpanInLoop;
            }
        }
        
        if (rv_index >= rb->writtenLength) {
            break;
        }

        const LSGReservedCommand_t* rcmd = &rb->array[rv_index];
        const int64_t rt = rcmd->tick + tOffset;

        if (rt >= startTick && rt < endTick) {
            const int64_t dt = rt - startTick;
            const int buf_offset = (int)(dt / kChannelCommandInterval);
            lsg_put_channel_command_and_clear_later_internal(ch, buf_offset, rcmd->cmd);
            ++rb->readPosition;
        } else if (rt > endTick) {
            break;
        }
    }
    
    return LSG_OK;
}

// Stocked waves and generators - - - - - - - - - - - -

#define kGoodMaxVolume (2205 * 6)

LSGStatus lsg_generate_triangle(int generatorBufferIndex) {
    if (!(generator_index_in_range( generatorBufferIndex ))) {
        return LSGERR_PARAM_OUTBOUND;
    }
    
    int i, pos;
    const int seglen = kLSGNumGeneratorSamples / 4;
    const int step = (kGoodMaxVolume * 24) / seglen;
    LSGSample* p = sGeneratorBuffers[generatorBufferIndex];

    pos = 0;
    
    //  1/4
    for (i = 0;i < seglen;++i) {
        p[pos++] = (i * step) >> 4;
    }

    const int maxvol = p[pos-1];
    // 2/4
    for (i = 0;i < seglen;++i) {
        p[pos++] = maxvol - p[i];
    }

    // 3/4, 4/4
    for (i = 0;i < (seglen << 1);++i) {
        p[pos++] = -p[i];
    }

    return LSG_OK;
}

LSGStatus lsg_generate_square(int generatorBufferIndex) {
    if (!(generator_index_in_range( generatorBufferIndex ))) {
        return LSGERR_PARAM_OUTBOUND;
    }

    const int seglen = kLSGNumGeneratorSamples / 2;
    int pos1 = 0;
    int pos2 = seglen;
    LSGSample* p = sGeneratorBuffers[generatorBufferIndex];

    for (int i = 0;i < seglen;++i) {
        p[pos1++] = kGoodMaxVolume;
        p[pos2++] = -kGoodMaxVolume;
    }
    
    lsg_apply_generator_filter(generatorBufferIndex);
    return LSG_OK;
}

LSGStatus lsg_generate_square_13(int generatorBufferIndex) {
    if (!(generator_index_in_range( generatorBufferIndex ))) {
        return LSGERR_PARAM_OUTBOUND;
    }

    const int seglen = kLSGNumGeneratorSamples / 4;
    int pos1 = 0;
    int pos2 = seglen;
    int pos3 = seglen*2;
    int pos4 = seglen*3;
    LSGSample* p = sGeneratorBuffers[generatorBufferIndex];
    
    for (int i = 0;i < seglen;++i) {
        p[pos1++] = kGoodMaxVolume;
        p[pos2++] = -kGoodMaxVolume;
        p[pos3++] = -kGoodMaxVolume;
        p[pos4++] = -kGoodMaxVolume;
    }
    
    lsg_apply_generator_filter(generatorBufferIndex);
    return LSG_OK;
}

LSGStatus lsg_generate_square_2114(int generatorBufferIndex) {
    if (!(generator_index_in_range( generatorBufferIndex ))) {
        return LSGERR_PARAM_OUTBOUND;
    }

    LSGSample* p = sGeneratorBuffers[generatorBufferIndex];
    for (int i = 0;i < kLSGNumGeneratorSamples;++i) {
        const int ph = (i << 3) / kLSGNumGeneratorSamples;
        if (ph == 0 || ph == 1 || ph == 3) {
            *p++ = kGoodMaxVolume;
        } else {
            *p++ = -kGoodMaxVolume;
        }
    }
    
    lsg_apply_generator_filter(generatorBufferIndex);
    return LSG_OK;
}

LSGStatus lsg_generate_short_noise(int generatorBufferIndex) {
    if (!(generator_index_in_range( generatorBufferIndex ))) {
        return LSGERR_PARAM_OUTBOUND;
    }
    
    LSGSample* p = sGeneratorBuffers[generatorBufferIndex];
    
    const int seglen = kLSGNumGeneratorSamples / 40;
    unsigned short reg = kBinNoiseFeedback;
    for (int i = 0;i < seglen;++i) {
        if (((reg & kBinNoiseTap1) != 0) != ((reg & kBinNoiseTap2) != 0)) {
            reg = (reg >> 1) | kBinNoiseFeedback;
        } else {
            reg >>= 1;
        }

        int val = ((reg % 5) - 2) * kGoodMaxVolume / 2;
        for (int j = 0;j < 40;++j) {
            *p++ = val;
        }
    }
    
    return LSG_OK;
}

LSGStatus lsg_generate_sin(int generatorBufferIndex, float a1, float a2, float a3, float a4, float a5, float a8, float a16) {
    if (!(generator_index_in_range( generatorBufferIndex ))) {
        return LSGERR_PARAM_OUTBOUND;
    }

    LSGSample* p = sGeneratorBuffers[generatorBufferIndex];
    const float DPI = M_PI * 2.0f;
    
    const int seglen = kLSGNumGeneratorSamples;
    for (int i = 0;i < seglen;++i) {
        const float t = (float)i / (float)seglen;
        *p++ = (int)((
         sinf(DPI * t       ) * a1 +
         sinf(DPI * t * 2.0f) * a2 +
         sinf(DPI * t * 3.0f) * a3 +
         sinf(DPI * t * 4.0f) * a4 +
         sinf(DPI * t * 5.0f) * a5 +
         sinf(DPI * t * 8.0f) * a8 +
         sinf(DPI * t *16.0f) * a16) * (double)kGoodMaxVolume);
    }

    return LSG_OK;
}

LSGStatus lsg_generate_sin_v(int generatorBufferIndex, const float* coefficients, unsigned int count) {
    if (!(generator_index_in_range( generatorBufferIndex ))) {
        return LSGERR_PARAM_OUTBOUND;
    }

    LSGSample* p = sGeneratorBuffers[generatorBufferIndex];
    const float DPI = M_PI * 2.0f;
    
    const int seglen = kLSGNumGeneratorSamples;
    for (int i = 0;i < seglen;++i) {
        const float t = (float)i / (float)seglen;
        
        float y = 0;
        for (int k = 0;k < count;++k) {
            y += sinf(DPI * t * (float)(k+1)) * coefficients[k];
        }
        
        y *= kGoodMaxVolume;
        if (y > 32767) {y = 32767;}
        else if (y < -32767) {y = -32767;}
        
        *p++ = y;
    }
    
    return LSG_OK;
}

LSGStatus lsg_generate_mixed(int generatorBufferIndex, int sourceGeneratorIndex1, int sourceGeneratorIndex2) {
    if (!(generator_index_good( generatorBufferIndex )) ||
        !(generator_index_good( sourceGeneratorIndex1 )) ||
        !(generator_index_good( sourceGeneratorIndex2 )) ) {
        return LSGERR_PARAM_OUTBOUND;
    }
    
    LSGSample* p = sGeneratorBuffers[generatorBufferIndex];
    const int len = kLSGNumGeneratorSamples;
    for (int i = 0;i < len;++i) {
        *p++ = (lsg_get_generator_buffer_sample(sourceGeneratorIndex1, i) + lsg_get_generator_buffer_sample(sourceGeneratorIndex2, i)) >> 1;
    }

    return LSG_OK;
}

LSGSample lsg_get_generator_buffer_sample(int generatorBufferIndex, int sampleIndex) {
    if (generatorBufferIndex == kLSGWhiteNoiseGeneratorSpecialIndex) {
        return lsg_channel_noise_next(&sChannelStatuses[0]);
    }

    if (generatorBufferIndex < 0 || generatorBufferIndex >= kLSGNumGenerators || sampleIndex < 0 || sampleIndex >= kLSGNumGeneratorSamples) {
        return 0;
    }
    
    LSGSample* p = sGeneratorBuffers[generatorBufferIndex];
    return p[sampleIndex];
}

LSGStatus lsg_apply_generator_filter(int generatorIndex) {
    if (!generator_index_good( generatorIndex )) {
        return LSGERR_PARAM_OUTBOUND;
    }
    
    const int flen = 63;
    const int buflen = kLSGNumGeneratorSamples;
    LSGSample* p = sGeneratorBuffers[generatorIndex];
    LSGSample* tmp = sGeneratorTempBuf;
    memcpy(tmp, p, sizeof(LSGSample) * buflen);
    
    for (int i = 0;i < buflen;++i) {
        float sum = 0;
        for (int j = 0;j < flen;++j) {
            const int pos = (i + buflen - j*1000) % buflen;
            sum += (float)tmp[pos] * sFIRTable63[j];
        }
        
        //fprintf(stdout, "%d, %d\n", p[i], (int)sum);
        p[i] = sum;
    }
    
    return LSG_OK;
}

LSGStatus lsg_channel_bind_rsvcmd(int channelIndex, LSGReservedCommandBuffer_t* pRCBuf) {
    if (!channel_index_in_range(channelIndex)) {
        return LSGERR_PARAM_OUTBOUND;
    }
    
    sChannelStatuses[channelIndex].pReservedCommandBuffer = pRCBuf;

    return LSG_OK;
}


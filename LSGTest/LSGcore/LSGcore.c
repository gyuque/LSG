#include <stdio.h>
#define _USE_MATH_DEFINES
#include <math.h>
#include "LSG.h"
#define generator_index_in_range(x) ((x) >= 0 && (x) < kLSGNumGenerators)
#define generator_index_good(x) (((x) >= 0 && (x) < kLSGNumGenerators) || (x) == kLSGWhiteNoiseGeneratorSpecialIndex)
#define channel_index_in_range(x) ((x) >= 0 && (x) < kLSGNumOutChannels)

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

#define kBinNoiseFeedback 0x4000
#define kBinNoiseTap1     0x01
#define kBinNoiseTap2     0x02

static int64_t sGlobalTick = 0;
static LSGSample sGeneratorBuffers[kLSGNumGenerators][kLSGNumGeneratorSamples];
static LSGChannel_t sChannelStatuses[kLSGNumOutChannels];

static LSGStatus lsg_initialize_channel(LSGChannel_t* ch);
static LSGStatus lsg_initialize_channel_command_buffer(LSGChannel_t* ch);
static LSGStatus lsg_initialize_generators();
static LSGStatus lsg_fill_generator_buffer(LSGSample* buf, size_t len, LSGSample val);
static LSGStatus lsg_apply_channel_command(LSGChannel_t* ch, ChannelCommand cmd, int commandOffsetPosition);
static LSGSample lsg_calc_channel_gain(LSGChannel_t* ch);
static LSGStatus lsg_fill_reserved_commands(int64_t startTick, LSGChannel_t* ch);

LSGStatus lsg_initialize() {
    sGlobalTick = 0;

    if (lsg_initialize_generators() != LSG_OK) {
        return LSGERR_GENERIC;
    }
    
    for (int i = 0;i < kLSGNumOutChannels;++i) {
        lsg_initialize_channel(&sChannelStatuses[i]);
        sChannelStatuses[i].selfIndex = i;
    }
    
    return LSG_OK;
}

int64_t lsg_get_global_tick() {
    return sGlobalTick;
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

LSGStatus lsg_initialize_channel(LSGChannel_t* ch) {
    ch->fq = 440;
    ch->global_detune = 0;
    ch->volume = kLSGChannelVolumeMax;
    ch->global_volume = kLSGChannelVolumeMax;
//    ch->fq = 261.625565f;
//    ch->fq = 293.66476f;
    ch->generatorIndex = 0;
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
    return LSG_OK;
}

LSGStatus lsg_initialize_channel_command_buffer(LSGChannel_t* ch) {
    for (int i = 0;i < kChannelCommandBufferLength;++i) {
        ch->commandRingBuffer[i] = 0;
    }
    
    ch->ringHeadPos = 0;
    
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

LSGStatus lsg_apply_channel_command(LSGChannel_t* ch, ChannelCommand cmd, int commandOffsetPosition) {
    if ((cmd & kLSGCommandBit_Enable) == 0) {
        return LSG_OK;
    }
    
    const int keyon = cmd & kLSGCommandBit_KeyOn;
    if (!keyon) {
        ch->keyonCount = -1;
    } else {
        ch->keyonCount = 0;
        ch->adsrPhase = 0;
    }
    
    if (cmd & kLSGCommandBit_Volume) {
        ch->volume = (cmd & kLSGCommandMask_Volume) >> 16;
    }
    
    const int noteNo = cmd & kLSGCommandMask_NoteNum;
    if (noteNo) {
        const int oct  = noteNo / 12;
        const int nidx = noteNo % 12;
        float base_fq = sNoteTable[nidx] * powf(2, oct) * 0.25f;
        if (noteNo == 1) {
            base_fq /= 10.0f;
        }
        
        const uint32_t pitchbits = cmd & (kLSGCommandMask_Pitch | kLSGCommandMask_PitchParam);
        if (pitchbits) {
            const int is_up = pitchbits & kLSGCommandBit_PitchUp;
            const int pitch_amount = (pitchbits >> 8) & 0x3f;
            
            if (is_up) {
//                printf(">>>>>> %d\n", pitch_amount);
                base_fq += base_fq * (float)pitch_amount / 63.0f;
            } else {
                base_fq -= (base_fq * 0.5f) * (float)pitch_amount / 63.0f;
            }
        }
        
        ch->fq = base_fq;
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

LSGStatus lsg_fill_generator_buffer(LSGSample* buf, size_t len, LSGSample val) {
    size_t i;
    
    for (i = 0;i < len;++i) {
        buf[i] = val;
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
        int should_fetch_command = ((sGlobalTick % (uint64_t)kChannelCommandInterval) == 0);
        for (ci = 0;ci < kLSGNumOutChannels;++ci) {
            LSGChannel_t* ch = &sChannelStatuses[ci];
            if (should_fetch_command) {
                const ChannelCommand cmd = lsg_consume_channel_command_buffer(ch);
if (cmd & kLSGCommandBit_Enable)
fprintf(stderr, "Ch: %2d   CMD: %x   t:%8lld\n", ci, cmd, sGlobalTick);
                lsg_apply_channel_command(ch, cmd, i);
            }
            
            lsg_apply_channel_adsr(ch);
            lsg_advance_channel_state(ch);

            const int fstep = (ch->fq + ch->global_detune) / baseFQ;
            ch->readPos = (ch->readPos + fstep) % kLSGNumGeneratorSamples;
            val += (lsg_calc_channel_gain(ch) * ch->volume * ch->global_volume) / vmax2;
        }

        if (val > 32767) { val = 32767; }
        else if (val < -32767) { val = -32767; }
        
        // Write   - - - - - - - - - - - - - - -
        pOut[writePos+Hi] = (val & 0xff00) >> 8;
        pOut[writePos+Lo] =  val & 0xff;
        if (bStereo) {
            pOut[writePos+2+Hi] = (val & 0xff00) >> 8;
            pOut[writePos+2+Lo] =  val & 0xff;
        }
        
        writePos += strideBytes;
        
        ++sGlobalTick;
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
    
    const int64_t endTick = startTick + (kChannelCommandBufferLength * kChannelCommandInterval);
    
    LSGReservedCommandBuffer_t* rb = ch->pReservedCommandBuffer;
    for (int i = 0;i < peek_max;++i) {
        const int rv_index = rb->readPosition;
        if (rv_index >= rb->writtenLength) {
            break;
        }
        
        const LSGReservedCommand_t* rcmd = &rb->array[rv_index];
        const int64_t rt = rcmd->tick;
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
    const double DPI = M_PI * 2.0;
    
    const int seglen = kLSGNumGeneratorSamples;
    for (int i = 0;i < seglen;++i) {
        const float t = (float)i / (float)seglen;
        *p++ = (int)((
         sin(DPI * t      ) * a1 +
         sin(DPI * t * 2.0) * a2 +
         sin(DPI * t * 3.0) * a3 +
         sin(DPI * t * 4.0) * a4 +
         sin(DPI * t * 5.0) * a5 +
         sin(DPI * t * 8.0) * a8 +
         sin(DPI * t *16.0) * a16) * (double)kGoodMaxVolume);
    }

    return LSG_OK;
}

LSGStatus lsg_generate_sin_v(int generatorBufferIndex, const float* coefficients, unsigned int count) {
    if (!(generator_index_in_range( generatorBufferIndex ))) {
        return LSGERR_PARAM_OUTBOUND;
    }

    LSGSample* p = sGeneratorBuffers[generatorBufferIndex];
    const double DPI = M_PI * 2.0;
    
    const int seglen = kLSGNumGeneratorSamples;
    for (int i = 0;i < seglen;++i) {
        const float t = (float)i / (float)seglen;
        
        float y = 0;
        for (int k = 0;k < count;++k) {
            y += sin(DPI * t * (float)(k+1)) * coefficients[k];
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

LSGStatus lsg_channel_bind_rsvcmd(int channelIndex, LSGReservedCommandBuffer_t* pRCBuf) {
    if (!channel_index_in_range(channelIndex)) {
        return LSGERR_PARAM_OUTBOUND;
    }
    
    sChannelStatuses[channelIndex].pReservedCommandBuffer = pRCBuf;

    return LSG_OK;
}


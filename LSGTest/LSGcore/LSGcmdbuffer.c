#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "LSG.h"

typedef struct _MMLNote_t {
    int note;
    int dots;
    int divs;
} MMLNote_t;
static LSGStatus mmlReadNote(const char* mml, int* pos, MMLNote_t* pOutNote);


LSGStatus lsg_rsvcmd_init(LSGReservedCommandBuffer_t* pRCBuf, size_t length) {
    if (!pRCBuf) { return LSGERR_NULLPTR; }
    
    pRCBuf->readPosition = 0;
    pRCBuf->length = length;
    pRCBuf->writtenLength = 0;
    pRCBuf->array = (LSGReservedCommand_t*)malloc( sizeof(LSGReservedCommand_t) * length );
    
    return LSG_OK;
}

LSGStatus lsg_rsvcmd_destroy(LSGReservedCommandBuffer_t* pRCBuf) {
    if (!pRCBuf) { return LSGERR_NULLPTR; }

    free(pRCBuf->array);
    return LSG_OK;
}

LSGStatus lsg_rsvcmd_add(LSGReservedCommandBuffer_t* pRCBuf, ChannelCommand cmd, int64_t tick) {
    if (!pRCBuf) { return LSGERR_NULLPTR; }
    
    if (pRCBuf->writtenLength >= pRCBuf->length) {
        return LSGERR_BUFFER_FULL;
    }
    
    pRCBuf->array[pRCBuf->writtenLength].cmd = cmd;
    pRCBuf->array[pRCBuf->writtenLength].tick = tick;
    ++(pRCBuf->writtenLength);
    
    return LSG_OK;
}

LSGStatus lsg_rsvcmd_clear(LSGReservedCommandBuffer_t* pRCBuf) {
    if (!pRCBuf) { return LSGERR_NULLPTR; }
    pRCBuf->readPosition = 0;
    pRCBuf->writtenLength = 0;
    
    return LSG_OK;
}

LSGStatus lsg_rsvcmd_fill_mlf(LSGReservedCommandBuffer_t* pRCBufArray, int nRCBufs, MLFPlaySetup_t* pPlaySetup, int64_t originTime) {
    int ch;
    
    for (ch = 0;ch < kLSGNumOutChannels;++ch) {
        // refer rv buffer
        if (ch >= nRCBufs) { break; }
        LSGReservedCommandBuffer_t* rb = &pRCBufArray[ch];


        MappedMLFChannel_t* mappedCh = &pPlaySetup->chmap[ch];
        const int len = mappedCh->eventsLength;
        
        if (mappedCh->defaultADSR.attack_rate) {
            lsg_set_channel_adsr(ch, &(mappedCh->defaultADSR));
        }
        
        for (int i = 0;i < len;++i) {
            const MLFEvent_t* ev = &(mappedCh->sortedEvents[i]);
            
            ChannelCommand cmd = kLSGCommandBit_Enable;
            const int buft = ev->absoluteTicks * pPlaySetup->deltaScale;
            if (ev->type == ME_NoteOn) {
                int vol = ev->velocity;
                if (vol > 127) { vol = 127; }
                
                cmd |= kLSGCommandBit_KeyOn | ev->noteNo | kLSGCommandBit_Volume | (vol << 16);
                lsg_rsvcmd_add(rb, cmd, originTime + buft);
            } else if (ev->type == ME_NoteOff) {
                lsg_rsvcmd_add(rb, cmd, originTime + buft);
            }

        }
    }
    
    return LSG_OK;
}

#define kMMLBadNum -1
static int mmlReadNum(int* pOut, const char* pStr, int pos) {
    int minus = 0;
    int mul = 1;
    if (pStr[pos] == '-') {
        minus = 1;
        mul = -1;
    }
    
    const int k1 = pStr[pos   +minus] - '0';
    const int k2 = pStr[pos+1 +minus] - '0';
    const int k3 = pStr[pos+2 +minus] - '0';
    
    if (k1 < 0 || k1 > 9) { return kMMLBadNum; }
    
    if (k2 < 0 || k2 > 9) {
        *pOut = k1 * mul;
        return 1+minus;
    }
    
    if (k3 < 0 || k3 > 9) {
        *pOut = (k1*10 + k2)  * mul;
        return 2+minus;
    }
    
    *pOut = (k1*100 + k2*10 + k3)  * mul;
    return 3+minus;
}

static LSGStatus mmlReadNumberedCommand(const char* mml, int* pos, int* valueOut) {
    int numOut;
    const int numLen = mmlReadNum(&numOut, mml, *pos + 1);
    if (numLen == kMMLBadNum) { return LSGERR_BAD_MML; }
    
    // success
    *valueOut = numOut;
    *pos = *pos + 1 + numLen;
    
    return LSG_OK;
}

static int mmlCalcNoteDuration(int specifiedDivs, int wLen, int defaultDivs, int dots) {
    if (specifiedDivs < 1) {
        specifiedDivs = defaultDivs;
    }
    
    const int nlen = wLen / specifiedDivs;
    int res = nlen;
    
    if (dots > 0) {
        res += nlen / 2;
    }

    if (dots > 1) {
        res += nlen / 4;
    }

    return res;
}

static uint32_t makePitchBits(int d) {
    if (d == 1) {d=2;} else if (d == -1) {d=-2;}
    
    d /= 2;
    if (d > 63) {d=63;}
    
    if (d > 0) {
        return kLSGCommandBit_PitchUp | (d << 8);
    } else if (d < 0) {
        return kLSGCommandBit_PitchDown | ((-d) << 8);
    }
    
    return 0;
}

LSGStatus lsg_rsvcmd_from_mml(LSGReservedCommandBuffer_t* pRCBuf, int w_duration, const char* mml, int64_t originTick) {
    if (!pRCBuf) { return LSGERR_NULLPTR; }
    int64_t currentTick = originTick;
    
    // states
    int st_DefaultLen = 4;
    int st_Octave = 4;
    int st_Q = 16;
    int st_Tim = 0;
    int st_Vol = 10;
    int st_Detune = 0;
    
    int pos = 0;
    const int len = (int)strlen(mml);
    for (int i = 0;i < 65536;++i) {
        if (pos >= len) {
            break;
        }
        
        const int k1 = mml[pos];
        switch (k1) {
            case 'k': {
                if (mmlReadNumberedCommand(mml, &pos, &st_Detune) != LSG_OK) { return LSGERR_BAD_MML; }
                
                printf("k=%d\n", st_Detune);
                break;
            }
                
            case 'l': {
                if (mmlReadNumberedCommand(mml, &pos, &st_DefaultLen) != LSG_OK) { return LSGERR_BAD_MML; }

                printf("L=%d\n", st_DefaultLen);
                break;
            }

            case 'q': {
                if (mmlReadNumberedCommand(mml, &pos, &st_Q) != LSG_OK) { return LSGERR_BAD_MML; }
                
                printf("Q=%d\n", st_Q);
                break;
            }

            case 'o': {
                if (mmlReadNumberedCommand(mml, &pos, &st_Octave) != LSG_OK) { return LSGERR_BAD_MML; }
                
                printf("O=%d\n", st_Octave);
                break;
            }

            case '@': {
                if (mmlReadNumberedCommand(mml, &pos, &st_Tim) != LSG_OK) { return LSGERR_BAD_MML; }
                
                printf("@=%d\n", st_Tim);
                break;
            }

            case 'v': {
                if (mmlReadNumberedCommand(mml, &pos, &st_Vol) != LSG_OK) { return LSGERR_BAD_MML; }
                
                printf("v=%d\n", st_Vol);
                break;
            }

            case '<':
                ++st_Octave;
                ++pos;
                break;

            case '>':
                --st_Octave;
                ++pos;
                break;

            case ' ':
                // ignore
                ++pos;
                break;

            // Notes
            case 'c':
            case 'd':
            case 'e':
            case 'f':
            case 'g':
            case 'a':
            case 'b':
            {
                MMLNote_t nt;
                if (mmlReadNote(mml, &pos, &nt) != LSG_OK) {
                    return LSGERR_BAD_MML;
                }
                
                const int dur = mmlCalcNoteDuration(nt.divs, w_duration, st_DefaultLen, nt.dots);
                const int noteno = nt.note + 12 * st_Octave;
                const uint32_t pitchbits = makePitchBits(st_Detune);
                
                ChannelCommand on_cmd = kLSGCommandBit_Enable | kLSGCommandBit_KeyOn | pitchbits | noteno;
                lsg_rsvcmd_add(pRCBuf, on_cmd, currentTick);
                const int64_t off_t = currentTick + (dur * st_Q) / 16;
                ChannelCommand off_cmd = kLSGCommandBit_Enable;
                lsg_rsvcmd_add(pRCBuf, off_cmd, off_t);
                
                currentTick += dur;
                break;
            }

            case 'r': {
                int rlen = 0;
                int numLen = mmlReadNum(&rlen, mml, pos+1);
                if (numLen == kMMLBadNum) {
                    numLen = 0;
                }
                
                int r_dots = 0;
                if (mml[pos+1] == '.') { ++r_dots; }
                if (mml[pos+2] == '.') { ++r_dots; }

                const int dur = mmlCalcNoteDuration(rlen, w_duration, st_DefaultLen, r_dots);
                const ChannelCommand cmd = kLSGCommandBit_Enable;
                lsg_rsvcmd_add(pRCBuf, cmd, currentTick);
                currentTick += dur;

                pos += 1 + numLen + r_dots;
                break;
            }

            // not implemented
            case '%': {
                int numOut;
                const int numLen = mmlReadNum(&numOut, mml, pos+1);
                if (numLen == kMMLBadNum) { return LSGERR_BAD_MML; }
                
                pos += 1 + numLen;
                break;
            }

            case 's': {
                int numOut;
                const int numLen = mmlReadNum(&numOut, mml, pos+1);
                if (numLen == kMMLBadNum) { return LSGERR_BAD_MML; }
                
                pos += 1 + numLen;
                break;
            }

            default:
                printf("Unknown statement: %c\n", k1);
                return LSGERR_BAD_MML;
                break;
        }
    }
    
    return LSG_OK;
}


static const int WNOTE_MAP[] = {0, 2, 4, 5, 7, 9, 11};
LSGStatus mmlReadNote(const char* mml, int* pos, MMLNote_t* pOutNote) {
    int dots = 0;
    int noteLen = 0;
    printf("<%c>",mml[*pos]);
    int w_noteno = mml[*pos] - 'c';
    if (w_noteno < 0) { w_noteno += 7; }
    
    if (w_noteno < 0 || w_noteno > 6) {
        return LSGERR_BAD_MML;
    }
    
    ++(*pos);
    int nn = WNOTE_MAP[w_noteno];
    
    // [+-]?
    if (mml[*pos] == '+') {
        ++nn;
        ++(*pos);
    } else if (mml[*pos] == '-') {
        --nn;
        ++(*pos);
    }
    
    // Num?
    int num;
    const int numLen = mmlReadNum(&num, mml, *pos);
    if (numLen != kMMLBadNum) {
        *pos += numLen;
        noteLen = num;
    }
    
    // .?
    if (mml[*pos] == '.') {
        ++dots;
        ++(*pos);
    }
    
    if (mml[*pos] == '.') {
        ++dots;
        ++(*pos);
    }

    
    if (pOutNote) {
        pOutNote->note = nn;
        pOutNote->divs = noteLen;
        pOutNote->dots = dots;
    }
    
    printf("[[ %d - %d - %d ]]\n", nn, noteLen, dots);
    
    return LSG_OK;
}

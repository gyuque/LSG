#import "LSGios.h"
#include "LSG.h"

static void audioQueueOutputCallbackBridge(void *inUserData, AudioQueueRef inAQ, AudioQueueBufferRef inBuffer);
static void audioInterruptionListener(void* inUserData, UInt32 inInterruption);
static void initializeFFTContext(LSGIOS_FFT_t* pFFT, int nBits);
static void dumpFFTReverseTable(LSGIOS_FFT_t* pFFT);
static unsigned int calcLRReversedBits(unsigned int src, int nBits);

@implementation LSGios
@synthesize recordGain;
@synthesize nChannels;
@synthesize nSampleBits;
@synthesize bufferWatcher;

- (id)init {
    if (self = [super init]) {
        fftdat.bits = 0;
        fftdat.nSamples = 0;
        fftdat.reverseMap = NULL;
        fftdat.sinTable = fftdat.cosTable = fftdat.wavBuffer = fftdat.realBuffer = fftdat.imagBuffer = fftdat.spectrumBuffer = NULL;
        
        _audioQueue = NULL;
        lsg_initialize();
        [self prepareAudioQueue];
        self.recordGain = NO;
    }

    return self;
}

- (void)clearGainLog {
    for (int i = 0;i < kLSGIOS_LengthOfGainLog;++i) {
        gainLog.buffer[i] = 0;
        gainLog.tBuffer[i] = 0;
    }
    
    gainLog.nextWritePos = 0;
}

- (void)dealloc {
#if !__has_feature(objc_arc)
    [super dealloc];
#endif
    [self cleanFFTContext];
    [self disposeObjects];
}

#define SAFE_MEM_FREE(x) if(x){ free(x); x=NULL; }
- (void)cleanFFTContext {
    SAFE_MEM_FREE(fftdat.reverseMap);
    SAFE_MEM_FREE(fftdat.sinTable);
    SAFE_MEM_FREE(fftdat.cosTable);
    SAFE_MEM_FREE(fftdat.wavBuffer);
    SAFE_MEM_FREE(fftdat.realBuffer);
    SAFE_MEM_FREE(fftdat.imagBuffer);
    SAFE_MEM_FREE(fftdat.spectrumBuffer);
    
    fftdat.nSamples = 0;
    fftdat.bits = 0;
}

- (void)disposeObjects
{
    if (_audioQueue) {
        AudioQueueStop(_audioQueue, true);
        AudioQueueDispose(_audioQueue, true);
        _audioQueue = NULL;
    }
}

- (bool)prepareAudioQueue {
    const OSStatus init_rs = AudioSessionInitialize(NULL, NULL, audioInterruptionListener, (__bridge void*)self);
    if (init_rs != errSecSuccess) {
        NSLog(@"Warning!");
    }
    
    AudioStreamBasicDescription adesc;
    
    adesc.mSampleRate = 44100.0;
    adesc.mFormatID = kAudioFormatLinearPCM;
    adesc.mFormatFlags = kAudioFormatFlagIsBigEndian | kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    adesc.mBitsPerChannel = 16;
    adesc.mFramesPerPacket = 1;
    adesc.mChannelsPerFrame = 2;
    
    adesc.mBytesPerFrame = adesc.mBitsPerChannel / 8 * adesc.mChannelsPerFrame;
    adesc.mBytesPerPacket = adesc.mBytesPerFrame * adesc.mFramesPerPacket;
    adesc.mReserved = 0;
    
    // Save properties
    mUsedASBD = adesc;
    nSampleBits = adesc.mBitsPerChannel;
    nChannels = adesc.mChannelsPerFrame;
    
    OSStatus newq_status = AudioQueueNewOutput(&adesc, audioQueueOutputCallbackBridge, (__bridge void*)self, CFRunLoopGetCurrent(), kCFRunLoopCommonModes, 0, &_audioQueue);
    if (newq_status != noErr) {
        return false;
    }
    
    // one frame = 2(sizeof short) * 2(stereo)
    const int bufferSize = 4410*3;
    
    for (int i = 0;i < kLSGIOS_NumOfBuffers;++i) {
        OSStatus buf_status = AudioQueueAllocateBuffer(_audioQueue, bufferSize, &_audioBufferList[i]);
        if (buf_status != noErr) {
            return false;
        }
    }
    
    mSampRate = adesc.mSampleRate;
    return true;
}

- (AudioStreamBasicDescription)asbd {
    return mUsedASBD;
}

- (void)start {
    for (int i = 0;i < kLSGIOS_NumOfBuffers;++i) {
        [self fillBuffer: _audioBufferList[i]];
    }
    AudioQueueStart(_audioQueue, NULL);
}

- (void)fillBuffer: (AudioQueueBufferRef)audioBuffer; {
    const int bufsize = audioBuffer->mAudioDataBytesCapacity;
    
    AudioStreamPacketDescription pac;
    pac.mStartOffset = 0;
    pac.mVariableFramesInPacket = 0;
    pac.mDataByteSize = bufsize;
    const int nSamples = bufsize / 4;
    
    unsigned char* pBuf = audioBuffer->mAudioData;
    /*
    for (int i = 0;i < nSamples;++i) {
        unsigned short val = (i % 147) * 30 - 100;
        pBuf[i*4  ] = (val & 0xff00) >> 8;
        pBuf[i*4+1] = val & 0xff;
        
        pBuf[i*4+2] = (val & 0xff00) >> 8;
        pBuf[i*4+3] = val & 0xff;
    }
     */
    
    lsg_synthesize_BE16(pBuf, nSamples, 4, YES);
    if (recordGain && fftdat.nSamples) {
        [self fillGainLog:pBuf stride:4 count:fftdat.nSamples];
        if (nSamples >= 2048) {
            [self fillGainLog:pBuf + (nSamples * 2) /* n/2 * stride */ stride:4 count:fftdat.nSamples];
        }
    }
//NSLog(@"======= %d", nSamples);
    audioBuffer->mAudioDataByteSize = bufsize;
    
    AudioQueueEnqueueBuffer(_audioQueue, audioBuffer, 1, &pac);
    
    if (bufferWatcher) {
        [bufferWatcher lsgioswatcher_afterAudioBufferFilled:pBuf numOfSamples:nSamples];
    }
}

- (void)fillGainLog:(unsigned char*)pSamplesBuffer stride:(int)bytesStride count:(int)nSamplesCount{
    long tmp;
    int i;
//    unsigned char* bp = (unsigned char*)&s16val;
    
    float* wbuf = fftdat.wavBuffer;
    
    unsigned char* inp = pSamplesBuffer;
    for (i = 0;i < nSamplesCount;++i) {
        tmp = (inp[0] << 8) | inp[1];
        if (tmp & 0x8000) {
            tmp = 0 - (~(tmp - 1) & 0xffff);
        }
        
        wbuf[i] = tmp;
        inp += bytesStride;
    }
    
    float* Rs = fftdat.realBuffer;
    float* Is = fftdat.imagBuffer;
    
    // Initial values
    for (i = 0;i < nSamplesCount;++i) {
        Rs[i] = wbuf[ fftdat.reverseMap[i] ];
        Is[i] = 0;
        fftdat.spectrumBuffer[i] = 0;
    }
    
    int halfSize = 1;
    while ( halfSize < nSamplesCount ) {
        const float phaseShiftStepReal = fftdat.cosTable[halfSize];
        const float phaseShiftStepImag = fftdat.sinTable[halfSize];
        float currentPhaseShiftReal = 1.0f;
        float currentPhaseShiftImag = 0;
        const int h2 = halfSize << 1;
        
        for (int fftStep = 0; fftStep < halfSize; fftStep++) {
            i = fftStep;
            
            while (i < nSamplesCount) {
                const int ofs = i + halfSize;
                const float tr = (currentPhaseShiftReal * Rs[ofs]) - (currentPhaseShiftImag * Is[ofs]);
                const float ti = (currentPhaseShiftReal * Is[ofs]) + (currentPhaseShiftImag * Rs[ofs]);

                Rs[ofs] = Rs[i] - tr;
                Is[ofs] = Is[i] - ti;
                Rs[i] += tr;
                Is[i] += ti;
                
                i += h2;
            }
            
            const float tmpReal = currentPhaseShiftReal;
            currentPhaseShiftReal = (tmpReal * phaseShiftStepReal) - (currentPhaseShiftImag * phaseShiftStepImag);
            currentPhaseShiftImag = (tmpReal * phaseShiftStepImag) + (currentPhaseShiftImag * phaseShiftStepReal);
        }
        
        halfSize = h2;
    }
    
    i = nSamplesCount >> 1;
    while(i--) {
        fftdat.spectrumBuffer[i] =  2.0f * sqrtf(Rs[i] * Rs[i] + Is[i] * Is[i]) / (float)nSamplesCount;
    }
    
    // Pack
    UInt32 packed = 0;
    float* spec = fftdat.spectrumBuffer;

    int spos = 0;
    int nBand = 2;
    for (int i = 0;i < 8;++i) {
        int avg = 0;
        for (int j = 0;j < nBand;++j) {
            avg += spec[spos++];
        }
        
        avg /= 1536;
        if (avg > 15) { avg = 15; }
        packed |= avg << (i*4);
        
        nBand <<= 1;
    }
    //fprintf(stderr, "%d  %08X   %d\n", spos, packed, nSamplesCount);
    
    gainLog.buffer[ gainLog.nextWritePos ] = packed;
    gainLog.tBuffer[ gainLog.nextWritePos ] = lsg_get_global_tick();
    
    gainLog.nextWritePos = (gainLog.nextWritePos + 1) % kLSGIOS_LengthOfGainLog;
}

- (void)onSuspend
{
    AudioSessionSetActive(false);
    [self disposeObjects];
}

- (void)onResume
{
    [self disposeObjects];
    
    [self prepareAudioQueue];
    AudioSessionSetActive(true);
    [self start];
}

- (int64_t)deviceTime
{
    AudioTimeStamp ctime;
    AudioQueueDeviceGetCurrentTime(_audioQueue, &ctime);
    
    return (int64_t) ctime.mSampleTime;
}

- (unsigned int) outputSamplingRate
{
    return mSampRate;
}

- (void)setDSPEnabled:(BOOL)bEnabled
{
    if (bEnabled) {
        [self ensureFFTContext];
        self.recordGain = YES;
        [self clearGainLog];
    } else {
        self.recordGain = NO;
    }
}

- (void)ensureFFTContext
{
    if (!fftdat.reverseMap) {
        initializeFFTContext(&fftdat, 10);
    }
}

- (UInt32)getSpectrumLog: (int64_t)minTime {
    for (int i = 0;i < kLSGIOS_LengthOfGainLog;++i) {
        int pos = (kLSGIOS_LengthOfGainLog + gainLog.nextWritePos - 1 - i) % kLSGIOS_LengthOfGainLog;
        if (gainLog.tBuffer[pos] < minTime) {
            return gainLog.buffer[pos];
        }
    }
    
    return 0;
}

@end

void initializeFFTContext(LSGIOS_FFT_t* pFFT, int nBits) {
    pFFT->bits = nBits;
    
    const int nSamples = pow(2, nBits);
    pFFT->nSamples = nSamples;
    pFFT->reverseMap = malloc( sizeof(int) * nSamples );
    pFFT->sinTable = malloc( sizeof(float) * nSamples );
    pFFT->cosTable = malloc( sizeof(float) * nSamples );
    pFFT->wavBuffer = malloc( sizeof(float) * nSamples );
    pFFT->realBuffer = malloc( sizeof(float) * nSamples );
    pFFT->imagBuffer = malloc( sizeof(float) * nSamples );
    pFFT->spectrumBuffer = malloc( sizeof(float) * nSamples );

    int* tbl = pFFT->reverseMap;
    
    for (int i = 0;i < nSamples;++i) {
        tbl[i] = calcLRReversedBits(i, nBits);
        
        pFFT->sinTable[i] = sinf(-M_PI / (float)i);
        pFFT->cosTable[i] = cosf(-M_PI / (float)i);
        pFFT->wavBuffer[i] = 0;
        pFFT->realBuffer[i] = 0;
        pFFT->imagBuffer[i] = 0;
    }
}

void dumpFFTReverseTable(LSGIOS_FFT_t* pFFT) {
    const int n = pFFT->nSamples;
    int* tbl = pFFT->reverseMap;
    for (int i = 0;i < n;++i) {
        fprintf(stderr, "%04d ", tbl[i]);
        
        if (i == (n >> 1)) {
            fprintf(stderr, "\n");
        }
    }

    fprintf(stderr, "\n");
}

unsigned int calcLRReversedBits(unsigned int src, int nBits) {
    unsigned int res = 0;
    int high = nBits - 1;
    for (int i = 0;i < nBits;++i) {
        if ( (1 << i) & src ) {
            res |= 1 << (high - i);
        }
    }
    
    return res;
}


void audioQueueOutputCallbackBridge(void *inUserData, AudioQueueRef inAQ, AudioQueueBufferRef inBuffer) {
    LSGios* lsgios = (__bridge LSGios*)inUserData;
    [lsgios fillBuffer:inBuffer];
    // NSLog(@"fill %p", inBuffer);
}

void audioInterruptionListener(void* inUserData, UInt32 inInterruption) {
    if (inInterruption == kAudioSessionEndInterruption) {
        [(__bridge LSGios*)inUserData onResume];
    } else if (inInterruption == kAudioSessionBeginInterruption) {
        [(__bridge LSGios*)inUserData onSuspend];
    }
}


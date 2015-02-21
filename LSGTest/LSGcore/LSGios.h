#import <Foundation/Foundation.h>
#include <AudioToolbox/AudioToolbox.h>

#define kLSGIOS_NumOfBuffers 2
#define kLSGIOS_LengthOfGainLog 200

typedef struct LSGIOS_FFT_t {
    int bits;
    int nSamples;
    int* reverseMap;
    float* sinTable;
    float* cosTable;
    float* wavBuffer;
    
    float* realBuffer;
    float* imagBuffer;
    float* spectrumBuffer;
} LSGIOS_FFT_t;

@protocol LSGiOSBufferWatcher
@required
- (void) lsgioswatcher_afterAudioBufferFilled:(unsigned char*)pBuffer numOfSamples:(int)nSamples;
@end

@interface LSGios : NSObject {
    AudioQueueRef _audioQueue;
    AudioQueueBufferRef _audioBufferList[kLSGIOS_NumOfBuffers];

    struct {
        UInt32 buffer[kLSGIOS_LengthOfGainLog];
        int64_t tBuffer[kLSGIOS_LengthOfGainLog];
        int nextWritePos;
    } gainLog;
    
    LSGIOS_FFT_t fftdat;
    unsigned int mSampRate;
    AudioStreamBasicDescription mUsedASBD;
}

@property(nonatomic, assign) BOOL recordGain;
@property(readonly, nonatomic) unsigned int outputSamplingRate;
@property(readonly, nonatomic) unsigned int nSampleBits;
@property(readonly, nonatomic) unsigned int nChannels;
@property(readonly, nonatomic) AudioStreamBasicDescription asbd;
@property(assign, nonatomic) id<LSGiOSBufferWatcher> bufferWatcher;

- (id)init;
- (void)cleanFFTContext;
- (bool)prepareAudioQueue;
- (void)start;
- (void)fillBuffer: (AudioQueueBufferRef)audioBuffer;

- (void)onSuspend;
- (void)onResume;
- (void)disposeObjects;
- (void)fillGainLog:(unsigned char*)pSamplesBuffer stride:(int)bytesStride count:(int)nSamplesCount;

- (int64_t)deviceTime;
- (void)setDSPEnabled:(BOOL)bEnabled;
- (UInt32)getSpectrumLog: (int64_t)minTime;

- (void)ensureFFTContext;
- (void)clearGainLog;

@end

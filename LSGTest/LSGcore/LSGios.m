#import "LSGios.h"
#include "LSG.h"

static void audioQueueOutputCallbackBridge(void *inUserData, AudioQueueRef inAQ, AudioQueueBufferRef inBuffer);
static void audioInterruptionListener(void* inUserData, UInt32 inInterruption);

@implementation LSGios

- (id)init {
    if (self = [super init]) {
        _audioQueue = NULL;
        lsg_initialize();
        [self prepareAudioQueue];
    }

    return self;
}

- (void)dealloc {
    [self disposeObjects];
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
    
    return true;
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
    
    audioBuffer->mAudioDataByteSize = bufsize;
    
    AudioQueueEnqueueBuffer(_audioQueue, audioBuffer, 1, &pac);
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

@end

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


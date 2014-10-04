#import <Foundation/Foundation.h>
#include <AudioToolbox/AudioToolbox.h>

#define kLSGIOS_NumOfBuffers 2

@interface LSGios : NSObject {
    AudioQueueRef _audioQueue;
    AudioQueueBufferRef _audioBufferList[kLSGIOS_NumOfBuffers];
}

- (id)init;
- (bool)prepareAudioQueue;
- (void)start;
- (void)fillBuffer: (AudioQueueBufferRef)audioBuffer;

- (void)onSuspend;
- (void)onResume;
- (void)disposeObjects;

@end

// LSG ONGEN - - - Laotour Sound Generator
// Version 0.2
// 2014 Satoshi Ueyama

#ifndef LSGTest_LSG_h
#define LSGTest_LSG_h
#include <stdint.h>

#ifdef _MSC_VER
#define LSG_INLINE __inline
#else
#define LSG_INLINE inline
#endif

typedef short LSGSample;
typedef int LSGStatus;

typedef uint32_t ChannelCommand;
#define kChannelCommandBufferLength 441
#define kChannelCommandInterval 100
#define kChannelFIRLength 9

typedef struct _LSG_ADSR {
    int attack_rate;
    int decay_rate;
    int sustain_level;
    int release_rate;
    int fade_rate;
} LSG_ADSR;

typedef void (*lsg_channel_command_executed_callback)(void* userData, int channelIndex, ChannelCommand cmd, int timeOffset);

typedef struct _LSGChannel_t {
    int selfIndex;
    
    int generatorIndex;
    int customNoteIndex;
    LSG_ADSR adsr;
    
    int readPos;
    float global_detune;
    int global_volume;
    int system_volume;
    int system_vol_dest;
    float fq, bent_fq;
    int lastNote;
    int volume;
    int currentBaseGain4X;
    int keyonCount;
    int adsrPhase;
    unsigned short noiseRegister;
    LSGSample fir_buf[kChannelFIRLength];
    
    ChannelCommand commandRingBuffer[kChannelCommandBufferLength];
    int ringHeadPos;
    lsg_channel_command_executed_callback exec_callback;
    void* userDataForCallback;
    
    struct _LSGReservedCommandBuffer_t* pReservedCommandBuffer;
} LSGChannel_t;

typedef struct _LSGReservedCommand_t {
    int64_t tick;
    ChannelCommand cmd;
} LSGReservedCommand_t;

typedef struct _LSGReservedCommandBuffer_t {
    size_t length;
    size_t writtenLength;
    int readPosition;
    size_t loopFirstIndex;
    size_t loopLastIndex;
    int64_t loopStartTime;
    int64_t loopEndTime;
    LSGReservedCommand_t* array;
} LSGReservedCommandBuffer_t;

#define kLSGOutSamplingRate 44100
#define kLSGNumGenerators 13
#define kLSGNumGeneratorSamples (44100*8)
#define kLSGNumOutChannels 13
#define kLSGRawGainMax4X 131072
#define kLSGChannelVolumeMax 127

#define kLSGWhiteNoiseGeneratorSpecialIndex 999

#define kLSGLeftChannel  0
#define kLSGRightChannel 1

#define LSG_OK 0
#define LSGERR_PARAM_OUTBOUND (-10)
#define LSGERR_NULLPTR (-11)
#define LSGERR_BUFFER_FULL (-12)
#define LSGERR_BAD_MML (-13)
#define LSGERR_BAD_FILE (-14)
#define LSGERR_GENERIC (-99)

#define kLSGCommandBit_Enable   0x80000000
#define kLSGCommandBit_NoKey    0x40000000
#define kLSGCommandBit_KeyOn    0x00000080
#define kLSGCommandMask_NoteNum 0x0000007f

#define kLSGCommandBit_PitchUp     0x00004000
#define kLSGCommandBit_PitchDown   0x00008000
#define kLSGCommandMask_Pitch      (kLSGCommandBit_PitchUp | kLSGCommandBit_PitchDown)
#define kLSGCommandMask_PitchParam 0x00003f00

#define kLSGCommandBit_Volume     0x00800000
#define kLSGCommandMask_Volume    0x007f0000

#define kLSGNoteMappingLength 128

// MLF Types

typedef enum _MLFEventType {
	ME_NoteOff = 0x80,
	ME_NoteOn  = 0x90,
	ME_ProgramChange = 0xc0,
	ME_ControlChange = 0xb0,
    ME_Pitch         = 0xe0,
	
	ME_LoopMarker = 0xff06,
	ME_Tempo = 0xff51,
	ME_MetaEventUnknown = 0xfffe,
	ME_Unknown = 0xffff,
	ME_RunningStatus = 0x10000
} MLFEventType;

typedef struct _MLFLoopDesc {
	uint32_t startTicks;
	uint32_t endTicks;
} MLFLoopDesc;

typedef struct _MLFEvent_t {
	uint32_t waitDelta;
	uint32_t absoluteTicks;
	
	MLFEventType type;
	int channel;
	int noteNo;
    int currentPitchBend;
	int velocity;
    
    int otherValue;
} MLFEvent_t;

typedef struct _MappedMLFChannel_t {
    MLFEvent_t* sortedEvents;
    int bEventsArrayIsStatic; // don't free memory
    int customNoteTableIndex;
    int eventsLength;
    int userData;
    
    LSG_ADSR defaultADSR;
} MappedMLFChannel_t;

typedef struct _MLFTrack_t {
	int index;
	size_t nEvents;
	MLFEvent_t* events_arr;
	
	int nCurrentCapacity;
	int nWritten;
} MLFTrack_t;

typedef struct _lsg_mlf_t {
	int format;
	int nTracks;
	int timeBase;
    int tempo;
    int drum_mapping_channel;
	
	MLFTrack_t* tracks_arr;
    MLFLoopDesc loopDesc;
} lsg_mlf_t;

typedef struct _MLFPlaySetup_t {
    int deltaScale;
    MLFLoopDesc loopDesc;
    MappedMLFChannel_t chmap[kLSGNumOutChannels];
} MLFPlaySetup_t;


// Public APIs
LSGStatus lsg_initialize();
LSGStatus lsg_channel_initialize_volume_params(int channelIndex);
LSGStatus lsg_synthesize_BE16(unsigned char* pOut, size_t nSamples, int strideBytes, const int bStereo);
LSGStatus lsg_synthesize_LE16(unsigned char* pOut, size_t nSamples, int strideBytes, const int bStereo);
LSGStatus lsg_set_channel_frequency(int channelIndex, float fq);
LSGStatus lsg_set_channel_global_detune(int channelIndex, float d);
LSGStatus lsg_set_channel_global_volume(int channelIndex, int v);
LSGStatus lsg_set_channel_source_generator(int channelIndex, int generatorBufferIndex);
LSGStatus lsg_set_channel_white_noise(int channelIndex);
LSGStatus lsg_get_channel_copy(int channelIndex, LSGChannel_t* pOut);
LSGStatus lsg_set_channel_adsr(int channelIndex, LSG_ADSR* pSourceADSR);
LSGStatus lsg_get_channel_adsr(int channelIndex, LSG_ADSR* pOutADSR);
LSGStatus lsg_noteoff_channel_immediately(int channelIndex);
LSGStatus lsg_apply_channel_adsr(LSGChannel_t* ch);
LSGStatus lsg_advance_channel_state(LSGChannel_t* ch);
LSGStatus lsg_set_channel_command_exec_callback(int channelIndex, lsg_channel_command_executed_callback callback, void* userData);
LSGStatus lsg_initialize_custom_note_table();
LSGStatus lsg_set_custom_note_frequency(int index, float fq);
LSGStatus lsg_use_custom_notes(int channelIndex, int customNotesIndex);

// fade control
LSGStatus lsg_set_channel_system_volume(int channelIndex, int vol);
LSGStatus lsg_set_channel_auto_fade(int channelIndex, int dest_vol);
LSGStatus lsg_set_channel_auto_fade_max(int channelIndex);

int64_t lsg_get_global_tick();

LSGStatus lsg_generate_triangle(int generatorBufferIndex);
LSGStatus lsg_generate_square(int generatorBufferIndex);
LSGStatus lsg_generate_square_13(int generatorBufferIndex);
LSGStatus lsg_generate_square_2114(int generatorBufferIndex);
LSGStatus lsg_generate_short_noise(int generatorBufferIndex);
LSGStatus lsg_generate_sin(int generatorBufferIndex, float a1, float a2, float a3, float a4, float a5, float a8, float a16);
LSGStatus lsg_generate_sin_v(int generatorBufferIndex, const float* coefficients, unsigned int count);
LSGStatus lsg_generate_mixed(int generatorBufferIndex, int sourceGeneratorIndex1, int sourceGeneratorIndex2);
ChannelCommand lsg_consume_channel_command_buffer(LSGChannel_t* ch);

LSGStatus lsg_put_channel_command_and_clear_later(int channelIndex, int offset, ChannelCommand cmd);

 // reserved commdnd API
LSGStatus lsg_rsvcmd_init(LSGReservedCommandBuffer_t* pRCBuf, size_t length);
LSGStatus lsg_rsvcmd_destroy(LSGReservedCommandBuffer_t* pRCBuf);
LSGStatus lsg_rsvcmd_add(LSGReservedCommandBuffer_t* pRCBuf, ChannelCommand cmd, int64_t tick);
LSGStatus lsg_rsvcmd_clear(LSGReservedCommandBuffer_t* pRCBuf);
LSGStatus lsg_channel_bind_rsvcmd(int channelIndex, LSGReservedCommandBuffer_t* pRCBuf);
LSGStatus lsg_rsvcmd_from_mml(LSGReservedCommandBuffer_t* pRCBuf, int w_duration, const char* mml, int64_t originTick);
LSGStatus lsg_rsvcmd_fill_mlf(LSGReservedCommandBuffer_t* pRCBufArray, int nRCBufs, MLFPlaySetup_t* pPlaySetup, int64_t originTime);

// MLF APIs
LSGStatus lsg_init_mlf(lsg_mlf_t* p_mlf_t);
LSGStatus lsg_load_mlf(lsg_mlf_t* p_mlf_t, const char* filename, int auto_drum_mapping_ch);
void lsg_free_mlf(lsg_mlf_t* p_mlf_t);

int lsg_mlf_count_channel_events(lsg_mlf_t* p_mlf_t, int channelIndex);
int lsg_mlf_count_channel_events_in_track(MLFTrack_t* p_track, int channelIndex);
MLFEvent_t* lsg_mlf_create_sorted_channel_events(lsg_mlf_t* p_mlf_t, int channelIndex);
void lsg_mlf_init_play_setup_struct(MLFPlaySetup_t* pSetup);
void lsg_mlf_destroy_play_setup_struct(MLFPlaySetup_t* pSetup);
void lsg_mlf_init_channel_mapping(MappedMLFChannel_t* ls, int count);
void lsg_mlf_destroy_channel_mapping(MappedMLFChannel_t* ls, int count);
int lsg_mlf_is_loop_valid(MLFLoopDesc* pLoop);

int lsg_util_calc_delta_time_scale(const lsg_mlf_t* p_mlf);

// Debug APIs
LSGSample lsg_get_generator_buffer_sample(int generatorBufferIndex, int sampleIndex);
void lsg_set_force_global_tick(int64_t t);

#endif

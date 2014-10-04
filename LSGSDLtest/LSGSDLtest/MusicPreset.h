#ifndef MusicPreset_h_included
#define MusicPreset_h_included
#include <yaml.h>
#include <map>
#include <vector>
#include <string>
extern "C" {
#include "../../LSGTest/LSGcore/LSGsdl.h"
}

typedef enum _ConfGeneratorType {
    G_SQUARE,
    G_SQUARE13,
    G_TRIANGLE,
    G_IFT,
    G_NOISE
} ConfGeneratorType;

typedef std::vector<float> WavCoefficientList;

typedef struct _MappedChannelConf {
    _MappedChannelConf() {
        midiCh = 0;
        volume = 1.0f;
        generatorType = G_SQUARE;
        detune = 0;
    }
    
    ConfGeneratorType generatorType;
    WavCoefficientList coefficients;
    int midiCh;
    float volume;
    float detune;
    LSG_ADSR adsr;
} MappedChannelConf;

typedef std::map<int, MappedChannelConf> ChannelConfMap;

class MusicPreset
{
public:
    MusicPreset();
    virtual ~MusicPreset();
    
    static const int kUnknownNode        = -1;
    static const int kTopSection_Input   = 1;
    static const int kTopSection_Mapping = 2;

    static const int kChannelConfiguration_Generator = 1;
    static const int kChannelConfiguration_MidiCh    = 2;
    static const int kChannelConfiguration_Volume    = 3;
    static const int kChannelConfiguration_Detune    = 4;
    static const int kChannelConfiguration_ADSR      = 5;

    bool loadFromYAMLFile(const char* filename);
    void dump();
    
    const char* getInputName() const;
    bool isChannelMapped(int lsgChannel) const;
    const MappedChannelConf& getChannelConf(int lsgChannel) const;
protected:
    void traverseRoot(yaml_document_t* ydoc, yaml_node_t* rootNode);
    bool readInput(yaml_document_t* ydoc, int valueNodeIndex);
    bool traverseChannelMapping(yaml_document_t* ydoc, int mappingNodeIndex);
    bool readChannelConfiguration(MappedChannelConf& outChConf, yaml_document_t* ydoc, yaml_node_t* mappingNode);
    static bool readADSR(LSG_ADSR& outADSR, yaml_document_t* ydoc, yaml_node_t* mappingNode);
    static bool readGeneratorCoefficients(WavCoefficientList& outVec, yaml_document_t* ydoc, yaml_node_t* seqNode);
    static int lookupTopLevelSectionType(yaml_node_t* node);
    static int lookupChannelConfigurationType(yaml_node_t* node);
    static int lookupChannelIndex(yaml_node_t* node);
    static ConfGeneratorType lookupGeneratorType(yaml_node_t* node);
    static float readFloat(yaml_node_t* node);
    static int readInt(yaml_node_t* node);
    static void readString(std::string& outStr, yaml_node_t* node);
    static const char* getGeneratorName(ConfGeneratorType t);
    
    std::string mInputName;
    ChannelConfMap mChannelMap;
};

#endif
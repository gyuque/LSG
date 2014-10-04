#include "MusicPreset.h"

MusicPreset::MusicPreset() {
}

MusicPreset::~MusicPreset() {
}

void MusicPreset::dump() {
    fprintf(stderr, "===========================================================\n");
    fprintf(stderr, "Input: %s\n", mInputName.c_str());
    fprintf(stderr, "===========================================================\n");

    for (int ch = 0;ch < 100;++ch) {
        if (mChannelMap.find(ch) != mChannelMap.end()) {
            const MappedChannelConf& chconf = mChannelMap[ch];
            
            fprintf(stderr, "LSG Channel %d\n", ch);
            fprintf(stderr, " MIDI ch=%d   Vol=%f   Detune=%f   Generator=%s\n",
                    chconf.midiCh, chconf.volume, chconf.detune, getGeneratorName(chconf.generatorType));
            
            if (chconf.adsr.attack_rate) {
                fprintf(stderr, " Custom ADSR  A:%d  D:%d  S:%d  R:%d  F:%d\n",
                        chconf.adsr.attack_rate,
                        chconf.adsr.decay_rate,
                        chconf.adsr.sustain_level,
                        chconf.adsr.release_rate,
                        chconf.adsr.fade_rate);
            }
            
            fprintf(stderr, "-----------------------------------------------------------\n");
        }
    }
}

const char* MusicPreset::getInputName() const {
    return mInputName.c_str();
}

bool MusicPreset::isChannelMapped(int lsgChannel) const {
    return mChannelMap.find(lsgChannel) != mChannelMap.end();
}

const MappedChannelConf& MusicPreset::getChannelConf(int lsgChannel) const {
    return mChannelMap.find(lsgChannel)->second;
}

bool MusicPreset::loadFromYAMLFile(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) {
        return false;
    }
    
    yaml_parser_t parser;
    yaml_document_t ydoc;
    yaml_parser_initialize(&parser);
    
    yaml_parser_set_input_file(&parser, fp);
    const int load_res = yaml_parser_load(&parser, &ydoc);
    if (load_res != 1) {
        fputs("Failed to load YAML file.", stderr);
        fclose(fp);
        yaml_parser_delete(&parser);
    }
    
    yaml_node_t* root = yaml_document_get_root_node(&ydoc);
    if (root->type != YAML_MAPPING_NODE) {
        fputs("Bad YAML content.", stderr);
        fclose(fp);
        yaml_parser_delete(&parser);
    }
    
    traverseRoot(&ydoc, root);
    
    fclose(fp);
    yaml_parser_delete(&parser);
    return true;
}

void MusicPreset::traverseRoot(yaml_document_t* ydoc, yaml_node_t* rootNode) {
    yaml_node_pair_t* pair = rootNode->data.mapping.pairs.start;
    const yaml_node_pair_t* afterEnd = rootNode->data.mapping.pairs.top;
    
    for (;pair < afterEnd;) {
        yaml_node_t* keyNode = yaml_document_get_node(ydoc, pair->key);
        const int sectionType = lookupTopLevelSectionType(keyNode);
        
        switch (sectionType) {
            case kTopSection_Input:
                readInput(ydoc, pair->value);
                break;
                
            case kTopSection_Mapping:
                traverseChannelMapping(ydoc, pair->value);
                break;
        }

        ++pair;
    }
}

bool MusicPreset::readInput(yaml_document_t* ydoc, int valueNodeIndex) {
    yaml_node_t* valueNode = yaml_document_get_node(ydoc, valueNodeIndex);
    if (valueNode->type != YAML_SCALAR_NODE) {
        return false;
    }
    
    readString(mInputName, valueNode);
    
    return true;
}

bool MusicPreset::traverseChannelMapping(yaml_document_t* ydoc, int mappingNodeIndex) {
    yaml_node_t* mappingNode = yaml_document_get_node(ydoc, mappingNodeIndex);
    if (mappingNode->type != YAML_MAPPING_NODE) {
        return false;
    }

    yaml_node_pair_t* pair = mappingNode->data.mapping.pairs.start;
    const yaml_node_pair_t* afterEnd = mappingNode->data.mapping.pairs.top;

    for (;pair < afterEnd;) {
        yaml_node_t* keyNode = yaml_document_get_node(ydoc, pair->key);
        const int channelIndex = lookupChannelIndex(keyNode);
        
        if (channelIndex >= 0) {
            MappedChannelConf chconf;
            yaml_node_t* valueNode = yaml_document_get_node(ydoc, pair->value);
            readChannelConfiguration(chconf, ydoc, valueNode);
            
            mChannelMap[channelIndex] = chconf;
        }
        
        ++pair;
    }

    return true;
}

bool MusicPreset::readChannelConfiguration(MappedChannelConf& outChConf, yaml_document_t* ydoc, yaml_node_t* mappingNode) {
    if (mappingNode->type != YAML_MAPPING_NODE) {
        return false;
    }
    
    LSG_ADSR& adsr = outChConf.adsr;
    adsr.attack_rate =
    adsr.decay_rate =
    adsr.sustain_level =
    adsr.release_rate =
    adsr.fade_rate = 0;

    yaml_node_pair_t* pair = mappingNode->data.mapping.pairs.start;
    const yaml_node_pair_t* afterEnd = mappingNode->data.mapping.pairs.top;
    for (;pair < afterEnd;) {
        yaml_node_t* keyNode = yaml_document_get_node(ydoc, pair->key);
        yaml_node_t* valueNode = yaml_document_get_node(ydoc, pair->value);
        const int confType = lookupChannelConfigurationType(keyNode);
        switch (confType) {
            case kChannelConfiguration_Generator:
                if (valueNode->type == YAML_SEQUENCE_NODE) {
                    outChConf.generatorType = G_IFT;
                    readGeneratorCoefficients(outChConf.coefficients, ydoc, valueNode);
                } else {
                    outChConf.generatorType = lookupGeneratorType(valueNode);
                }
                
                break;

            case kChannelConfiguration_MidiCh:
                outChConf.midiCh = readInt(valueNode);
                break;

            case kChannelConfiguration_Volume:
                outChConf.volume = readFloat(valueNode);
                break;

            case kChannelConfiguration_Detune:
                outChConf.detune = readFloat(valueNode);
                break;
                
            case kChannelConfiguration_ADSR:
                readADSR(adsr, ydoc, valueNode);
                break;
        }
        
        ++pair;
    }
    
    return true;
}

int MusicPreset::lookupTopLevelSectionType(yaml_node_t* node) {
    if (node->type != YAML_SCALAR_NODE) {
        return kUnknownNode;
    }
    
    const char k = node->data.scalar.value[0];
    if (k == 'i') {
        return kTopSection_Input;
    } else if (k == 'm') {
        return kTopSection_Mapping;
    }
    
    return kUnknownNode;
}

// parse ch0-ch99
int MusicPreset::lookupChannelIndex(yaml_node_t* node) {
    if (node->type != YAML_SCALAR_NODE) {
        return kUnknownNode;
    }
    
    char buf[3] = {0,0,0};
    yaml_char_t* raw = node->data.scalar.value;
    if (raw[0] != 'c') {
        return -1;
    }
    
    if (node->data.scalar.length > 2) {
        buf[0] = raw[2];
    }

    if (node->data.scalar.length > 3) {
        buf[0] = raw[3];
    }
    
    return atoi(buf);
}

int MusicPreset::lookupChannelConfigurationType(yaml_node_t* node) {
    if (node->type != YAML_SCALAR_NODE) {
        return kUnknownNode;
    }
    
    const char k = node->data.scalar.value[0];
    switch (k) {
        case 'g':
            return kChannelConfiguration_Generator; break;
        case 'm':
            return kChannelConfiguration_MidiCh; break;
        case 'v':
            return kChannelConfiguration_Volume; break;
        case 'd':
            return kChannelConfiguration_Detune; break;
        case 'a':
            return kChannelConfiguration_ADSR; break;
    }
    
    return kUnknownNode;
}

ConfGeneratorType MusicPreset::lookupGeneratorType(yaml_node_t* node) {
    if (node->type != YAML_SCALAR_NODE) {
        return G_SQUARE;
    }
    
    std::string name;
    readString(name, node);
    const char k = node->data.scalar.value[0];
    if (k == 's') {
        if (name.find('1') != std::string::npos) {
            return G_SQUARE13;
        }
        
        return G_SQUARE;
    } else if (k == 't') {
        return G_TRIANGLE;
    } else if (k == 'n') {
        return G_NOISE;
    }
    
    return G_SQUARE;
}

float MusicPreset::readFloat(yaml_node_t* node) {
    if (node->type != YAML_SCALAR_NODE) {
        return 0;
    }
    
    std::string s;
    readString(s, node);
    
    return (float)atof(s.c_str());
}

int MusicPreset::readInt(yaml_node_t* node) {
    if (node->type != YAML_SCALAR_NODE) {
        return 0;
    }
    
    std::string s;
    readString(s, node);
    
    return atoi(s.c_str());
}

void MusicPreset::readString(std::string& outStr, yaml_node_t* node) {
    char tmp[1024];
    int len = (int)node->data.scalar.length;
    if (len > 1023) { len = 1023; }
    memcpy(tmp, node->data.scalar.value, len);
    tmp[len] = '\0';
    
    outStr = tmp;
}

const char* MusicPreset::getGeneratorName(ConfGeneratorType t) {
    switch (t) {
        case G_SQUARE:
            return "square"; break;

        case G_SQUARE13:
            return "square(duty 1:3)"; break;

        case G_TRIANGLE:
            return "triangle"; break;

        case G_NOISE:
            return "noise"; break;
            
        case G_IFT:
            return "ift"; break;
            
        default:
            return "unknown"; break;
    }
}

bool MusicPreset::readADSR(LSG_ADSR& outADSR, yaml_document_t* ydoc, yaml_node_t* mappingNode) {
    if (mappingNode->type != YAML_MAPPING_NODE) {
        return false;
    }

    yaml_node_pair_t* pair = mappingNode->data.mapping.pairs.start;
    const yaml_node_pair_t* afterEnd = mappingNode->data.mapping.pairs.top;
    for (;pair < afterEnd;) {
        const yaml_node_t* keyNode = yaml_document_get_node(ydoc, pair->key);
        const int k = keyNode->data.scalar.value[0];

        yaml_node_t* valueNode = yaml_document_get_node(ydoc, pair->value);

        switch (k) {
            case 'a':
                outADSR.attack_rate = readInt(valueNode);
                break;
            case 'd':
                outADSR.decay_rate = readInt(valueNode);
                break;
            case 's':
                outADSR.sustain_level = readInt(valueNode);
                break;
            case 'r':
                outADSR.release_rate = readInt(valueNode);
                break;
            case 'f':
                outADSR.fade_rate = readInt(valueNode);
                break;
        }
        
        ++pair;
    }

    return true;
}

bool MusicPreset::readGeneratorCoefficients(WavCoefficientList& outVec, yaml_document_t* ydoc, yaml_node_t* seqNode) {
    yaml_node_item_t* item = seqNode->data.sequence.items.start;
    const yaml_node_item_t* afterEnd = seqNode->data.sequence.items.top;
    outVec.clear();

    for (;item < afterEnd;) {
        yaml_node_t* valueNode = yaml_document_get_node(ydoc, *item);
        outVec.push_back( readFloat(valueNode) );
        
        ++item;
    }
    
    return true;
}

#include <stdio.h>
#include <stdlib.h>
#include "LSG.h"

#define kInitialTrackCapacity 64
static const int s_mevents_verbose = 0;

static int check_smf_header(FILE* fp);
static size_t check_smf_track_header_and_size(FILE* fp);
static LSGStatus read_smf_header_chunk(FILE* fp, lsg_mlf_t* p_mlf_t);
static LSGStatus read_smf_allocate_tracks(lsg_mlf_t* p_mlf_t);
static LSGStatus read_smf_all_tracks(FILE* fp, lsg_mlf_t* p_mlf_t);
static LSGStatus read_smf_track(FILE* fp, lsg_mlf_t* p_mlf_t, int trackIndex);
static LSGStatus init_mlf_track_struct(MLFTrack_t* tr, int trackIndex);
static LSGStatus mlf_push_event(MLFTrack_t* tr, MLFEvent_t* ev);
static uint32_t read_smf_delta(FILE* fp, int* pOutReadBytes);
static LSGStatus read_smf_event(FILE* fp, int* pOutReadBytes, MLFEvent_t* pOutEv);
static LSGStatus read_smf_meta_event(FILE* fp, int metaEventType, int* pOutReadBytes, MLFEvent_t* pOutEv);
static LSGStatus calc_smf_absolute_time(MLFTrack_t* tr);
static LSGStatus pick_smf_markers(MLFTrack_t* tr, MLFLoopDesc* outLoopDesc);
static void smf_event_apply_running_state(MLFEvent_t* newEvent, const MLFEvent_t* prevEvent);
static void smf_event_apply_drum_mapping(MLFEvent_t* ev);
static int read_1blen_message(FILE* fp);
static int read_1blen_message_get_first(FILE* fp, int* outByte);

static size_t read_smf_u32(FILE* fp);
static size_t read_smf_u16(FILE* fp);
static size_t read_smf_be(FILE* fp, int nBytes);

LSGStatus lsg_init_mlf_loop(lsg_mlf_t* p_mlf_t) {
    p_mlf_t->loopDesc.startTicks = 0;
    p_mlf_t->loopDesc.endTicks = 0;
    return LSG_OK;
}

LSGStatus lsg_init_mlf(lsg_mlf_t* p_mlf_t) {
    p_mlf_t->nTracks = 0;
    p_mlf_t->tracks_arr = NULL;
    lsg_init_mlf_loop(p_mlf_t);
    return LSG_OK;
}

LSGStatus lsg_load_mlf(lsg_mlf_t* p_mlf_t, const char* filename, int auto_drum_mapping_ch) {
	FILE* fp;
	lsg_init_mlf(p_mlf_t);
    
    p_mlf_t->drum_mapping_channel = auto_drum_mapping_ch;
	p_mlf_t->tempo = 120;
	
	fp = fopen(filename, "rb");
	if (!fp) {
		return LSGERR_GENERIC;
	}

	// Check header
	const int is_smf = check_smf_header(fp);
	if (is_smf) {
		const size_t hsize = read_smf_u32(fp);
		if (hsize == 6) {
			read_smf_header_chunk(fp, p_mlf_t);
			read_smf_allocate_tracks(p_mlf_t);
			
			read_smf_all_tracks(fp, p_mlf_t);
		}
	}

	fclose(fp);
    return LSG_OK;
}

int lsg_util_calc_delta_time_scale(const lsg_mlf_t* p_mlf) {
    const float miditick_duration = (float)p_mlf->tempo / (float)p_mlf->timeBase;
    const float sample_duration = 1000000.0f / (float)kLSGOutSamplingRate;
    
    return miditick_duration / sample_duration;
}

LSGStatus read_smf_allocate_tracks(lsg_mlf_t* p_mlf_t) {
	const int n = p_mlf_t->nTracks;
	p_mlf_t->tracks_arr = (MLFTrack_t*) malloc( sizeof(MLFTrack_t) * n );
	fprintf(stderr, "%d tracks allocated\n", n);

	return LSG_OK;
}

void lsg_free_mlf(lsg_mlf_t* p_mlf_t) {
	int n = p_mlf_t->nTracks;
    if (!p_mlf_t->tracks_arr) { return; }

	for (int i = 0;i < n;++i) {
		MLFTrack_t* tr = &p_mlf_t->tracks_arr[i];
		if (tr->events_arr) {
			free(tr->events_arr);
			tr->events_arr = NULL;
		}
	}
	
	free(p_mlf_t->tracks_arr);
}

LSGStatus read_smf_header_chunk(FILE* fp, lsg_mlf_t* p_mlf_t) {
	p_mlf_t->format   = (int)read_smf_u16(fp);
	p_mlf_t->nTracks  = (int)read_smf_u16(fp);
	p_mlf_t->timeBase = (int)read_smf_u16(fp);
	
	printf("%d  %d  %d\n", p_mlf_t->format, p_mlf_t->nTracks, p_mlf_t->timeBase);
	
	return LSG_OK;
}

static int check_smf_fourcc(FILE* fp, const int* cc) {
	for (int i = 0;i < 4;++i) {
		if (fgetc(fp) != cc[i]) {
			return 0;
		}
	}
	
	return 1;
}

int check_smf_header(FILE* fp) {
	const int cc[] = {'M','T','h','d'};
	return check_smf_fourcc(fp, cc);
}


size_t check_smf_track_header_and_size(FILE* fp) {
	const int cc[] = {'M','T','r','k'};
	if (!check_smf_fourcc(fp, cc)) {
		return 0;
	}
	
	const int sz = (int)read_smf_u32(fp);
	return (size_t)sz;
}


LSGStatus read_smf_all_tracks(FILE* fp, lsg_mlf_t* p_mlf_t) {
	int ti;
	for (ti = 0;ti < p_mlf_t->nTracks;++ti) {
		printf("\n\n== Track %d\n", ti);
		read_smf_track(fp, p_mlf_t, ti);
	}
	
	return LSG_OK;
}

static LSGStatus allocate_mlf_track_events(MLFTrack_t* tr) {
	const size_t newSize = (tr->nCurrentCapacity == 0) ? kInitialTrackCapacity : (tr->nCurrentCapacity * 2);
	tr->events_arr = (MLFEvent_t*)realloc(tr->events_arr, sizeof(MLFEvent_t) * newSize);
	tr->nCurrentCapacity = (int)newSize;
	return LSG_OK;
}

LSGStatus init_mlf_track_struct(MLFTrack_t* tr, int trackIndex) {
	tr->index = trackIndex;
	tr->nEvents = 0;
	tr->events_arr = NULL;
	tr->nCurrentCapacity = 0;
	tr->nWritten = 0;
	
	return LSG_OK;
}

LSGStatus read_smf_track(FILE* fp, lsg_mlf_t* p_mlf_t, int trackIndex) {
	size_t tlen = check_smf_track_header_and_size(fp);
	if (tlen < 3) {
		return LSGERR_BAD_FILE;
	}
	
	if (trackIndex >= p_mlf_t->nTracks) {
		fprintf(stderr, "Bad track index\n");
		return LSGERR_GENERIC;
	}

	MLFTrack_t* track_data = &p_mlf_t->tracks_arr[trackIndex];
	init_mlf_track_struct(track_data, trackIndex);
	allocate_mlf_track_events(track_data);

	MLFEvent_t prevEv;
	
	int total_read_bytes = 0;
    int pitchBend = 0;
	for (int i = 0;i < 99999;++i) {
		int dt_bytes = 0;
		const uint32_t dt = read_smf_delta(fp, &dt_bytes);
		if (s_mevents_verbose) {
			fprintf(stderr, "D: %u @%d\n", dt, dt_bytes);
		}
		total_read_bytes += dt_bytes;
		
		MLFEvent_t tempEv;
		tempEv.waitDelta = dt;
		tempEv.type = ME_Unknown;
        tempEv.currentPitchBend = pitchBend;
		
		int ev_bytes = 0;
		read_smf_event(fp, &ev_bytes, &tempEv);
		smf_event_apply_running_state(&tempEv, &prevEv);
        if (tempEv.channel == p_mlf_t->drum_mapping_channel) {
            smf_event_apply_drum_mapping(&tempEv);
        }
        
        if (tempEv.type == ME_Tempo) {
            p_mlf_t->tempo = tempEv.otherValue;
        } else if (tempEv.type == ME_Pitch) {
            pitchBend = tempEv.currentPitchBend;
        }

        //printf("%d bytes advance\n", ev_bytes);
		total_read_bytes += ev_bytes;
		mlf_push_event(track_data, &tempEv);

		if (total_read_bytes >= tlen) {
			break;
		}
		
		prevEv = tempEv;
	}

	track_data->nEvents = track_data->nWritten;
	calc_smf_absolute_time(track_data);
    
    if (!lsg_mlf_is_loop_valid(&p_mlf_t->loopDesc)) {
        pick_smf_markers(track_data, &p_mlf_t->loopDesc);
    }
    //printf(":::::::::::::::::::: %d %d\n",trackIndex, track_data->nEvents);

	return LSG_OK;
}

LSGStatus calc_smf_absolute_time(MLFTrack_t* tr) {
	int sum = 0;
	const int n = (int)tr->nEvents;
	for (int i = 0;i < n;++i) {
		MLFEvent_t* ev = &tr->events_arr[i];
		sum += ev->waitDelta;
		
		ev->absoluteTicks = sum;
	}
    
	return LSG_OK;
}

LSGStatus pick_smf_markers(MLFTrack_t* tr, MLFLoopDesc* outLoopDesc) {
    int foundCount = 0;
	const int n = (int)tr->nEvents;
    int bPrevIsLoopStart = 0;
    
    // Initialize
    if (outLoopDesc) {
        outLoopDesc->startTicks = 0;
        outLoopDesc->endTicks = 0;
    }
    
	for (int i = 0;i < n;++i) {
		MLFEvent_t* ev = &tr->events_arr[i];
        if (ev->type == ME_LoopMarker) {
            ++foundCount;
            
            if (foundCount == 1) {
                // Found first
                bPrevIsLoopStart = 1;
            } else if (foundCount == 2) {
                // Found second
                if (outLoopDesc) {
                    // Write
                    outLoopDesc->endTicks = ev->absoluteTicks;
                }
            }
        } else {
            if (bPrevIsLoopStart) {
                bPrevIsLoopStart = 0;
                if (outLoopDesc) {
                    // Write
                    outLoopDesc->startTicks = ev->absoluteTicks;
                }
            }
        }
    }
    
    if (foundCount < 2) {
        return LSGERR_GENERIC;
    }
    
    if (outLoopDesc) {
        fprintf(stderr, " :Loop found: %d <-> %d\n", outLoopDesc->startTicks, outLoopDesc->endTicks);
    }
    
	return LSG_OK;
}

int lsg_mlf_is_loop_valid(MLFLoopDesc* pLoop) {
    return (pLoop->endTicks > pLoop->startTicks);
}

LSGStatus mlf_push_event(MLFTrack_t* tr, MLFEvent_t* ev) {
	if (tr->nWritten >= tr->nCurrentCapacity) {
		allocate_mlf_track_events(tr);
	}

	MLFEvent_t* ls = tr->events_arr;
	ls[ tr->nWritten ] = *ev;
	++(tr->nWritten);

	return LSG_OK;
}

uint32_t read_smf_delta(FILE* fp, int* pOutReadBytes) {
	uint32_t dt = 0;
	*pOutReadBytes = 0;

	for (int i = 0;i < 4;++i) {
		dt <<= 7;
		const int k = fgetc(fp);
		dt |= (k & 0x7f);
		if (pOutReadBytes) {
			++(*pOutReadBytes);
		}
		
		if ((k & 0x80) == 0) {
			break;
		}
	}

	return dt;
}

LSGStatus read_smf_event(FILE* fp, int* pOutReadBytes, MLFEvent_t* pOutEv) {
	const int st = fgetc(fp);
	*pOutReadBytes = 1;
	
	// printf("<%02X>\n", st);
	
	if (st == 0xff) {
		// meta events
		const int mt = fgetc(fp);
		++(*pOutReadBytes);
		
		int m_bytes = 0;
		pOutEv->type    = ME_MetaEventUnknown;
		pOutEv->channel = -1;
		read_smf_meta_event(fp, mt, &m_bytes, pOutEv);
		
		*pOutReadBytes += m_bytes;
	} else if (st & 0x80) {
		// new status
		int st_type = (st & 0xf0);
		int ch = st & 0x0f;
		
		const int nn = fgetc(fp);
		++(*pOutReadBytes);

		int found_2b = 1;
		switch(st_type) {
			case 0xc0:
			pOutEv->type    = ME_ProgramChange;
			pOutEv->channel = ch;

			printf("%02X%02X: PROGRAM CHANGE %d to %d\n", st, nn, ch, nn);
			break;

            case 0xd0:
                // pressure
                break;

			default:
			found_2b = 0;
			break;
		}

		if (!found_2b) {
			// 3B messages
			const int param = fgetc(fp);
			++(*pOutReadBytes);
		
			//printf("%02X%02X%02X: ", st, nn, param);
			switch(st_type) {
				case 0x80:
				
				pOutEv->type    = ME_NoteOff;
				pOutEv->channel = ch;
				pOutEv->noteNo  = nn;
				pOutEv->velocity= param;
				
				if (s_mevents_verbose) {
					fprintf(stderr, "NOTE OFF (ch:%d)  %d  %d\n", ch, nn, param);
				}
				break;

				case 0x90:
				
				pOutEv->type    = ME_NoteOn;
				pOutEv->channel = ch;
				pOutEv->noteNo  = nn;
				pOutEv->velocity= param;

				if (s_mevents_verbose) {
					fprintf(stderr, "NOTE ON   %d  %d\n", nn, param);
				}
				break;

				case 0xB0:
				pOutEv->type    = ME_ControlChange;
				pOutEv->channel = ch;
				printf("CTRLCHG   %d  %d\n", nn, param);
				break;

                case 0xE0: {
                    uint16_t raw_word = (nn&0x7f) | ((param&0x7f) << 7);
                    
                    pOutEv->type    = ME_Pitch;
                    pOutEv->channel = ch;
                    pOutEv->currentPitchBend = (int)raw_word - 0x2000;
                    // printf("  PitchBend: %d \n", pOutEv->otherValue);
                } break;

				default:
				printf("Unknown status! %02X ******************************\n", st_type);
				break;
			}
		}
	} else {
		// running status
		const int nn = st; // first byte is note no.

		const int param = fgetc(fp);
		++(*pOutReadBytes);
		
		//printf("r %02X%02X: ", nn, param);
		//puts("\n\n<<< RUNNING STATUS! >>>\n");

		pOutEv->type    = ME_RunningStatus;
		pOutEv->noteNo  = nn;
		pOutEv->velocity= param;
	}
	
	return LSG_OK;
}

void smf_event_apply_running_state(MLFEvent_t* newEvent, const MLFEvent_t* prevEvent) {
	if (newEvent->type == ME_RunningStatus) {
		newEvent->type = prevEvent->type;
		newEvent->channel = prevEvent->channel;
	}
}

void smf_event_apply_drum_mapping(MLFEvent_t* ev) {
    if (ev->noteNo == 38 || ev->noteNo == 40) {
        ev->noteNo = 1;
    } else {
        ev->noteNo = 120;
        ev->velocity = (ev->velocity*2) / 3;
    }
}

LSGStatus read_smf_meta_event(FILE* fp, int metaEventType, int* pOutReadBytes, MLFEvent_t* pOutEv) {
	switch(metaEventType) {
		case 0x01: {
			// free text
			*pOutReadBytes += read_1blen_message(fp);
		} break;
            
		case 0x02: {
			// copyright
			*pOutReadBytes += read_1blen_message(fp);
		} break;

		case 0x03: {
			// title
			*pOutReadBytes += read_1blen_message(fp);
		} break;
            
		case 0x06: {
			// marker
            int body_first_val;
            const int advance_len = read_1blen_message_get_first(fp, &body_first_val);
			*pOutReadBytes += advance_len;
            if (body_first_val == 1 && advance_len == 2) {
                pOutEv->type = ME_LoopMarker;
            }
		} break;

		case 0x20: {
			fgetc(fp); // 01
			fgetc(fp); // ch num
			*pOutReadBytes += 2;
		} break;

		case 0x21: {
			fgetc(fp); // 01
			fgetc(fp); // port num
			*pOutReadBytes += 2;
		} break;
		
		case 0x2f: {
			fgetc(fp); // 00
			*pOutReadBytes += 1;
			
			puts(": Track end :");
		} break;
		
		case 0x58: {
			// rhythm
			fgetc(fp); // 04
			fgetc(fp);
			fgetc(fp);
			fgetc(fp);
			fgetc(fp);
			*pOutReadBytes += 5;
		} break;
		
		case 0x59: {
			fgetc(fp); // 02
			fgetc(fp);
			fgetc(fp);
			
			*pOutReadBytes += 3;
		} break;
		
		case 0x51: {
			// tempo
			fgetc(fp); // 03
            
			const int b3 = fgetc(fp);
			const int b2 = fgetc(fp);
			const int b1 = fgetc(fp);
			int tms = (b3 << 16) | (b2 << 8) | b1;
            
            pOutEv->type = ME_Tempo;
            pOutEv->otherValue = tms;
            
			*pOutReadBytes += 4;
		} break;
		
		case 0x7f: {
			// sequencer dep event
			*pOutReadBytes += read_1blen_message(fp);
		} break;
		
		default: {
			*pOutReadBytes += read_1blen_message(fp);
            fprintf(stderr, "WARNING: UNKNOWN META EVENT: %02X ******************************\n", metaEventType);
        }
		break;
	}
	
	return LSG_OK;
}

int lsg_mlf_count_channel_events(lsg_mlf_t* p_mlf_t, int channelIndex) {
	const int n = p_mlf_t->nTracks;
	int sum = 0;
	for (int i = 0;i < n;++i) {
		sum += lsg_mlf_count_channel_events_in_track(&p_mlf_t->tracks_arr[i], channelIndex);
	}
	
	return sum;
}

int lsg_mlf_count_channel_events_in_track(MLFTrack_t* p_track, int channelIndex) {
	const int n = (int)p_track->nEvents;
	MLFEvent_t* ls = p_track->events_arr;

	int sum = 0;
	for (int i = 0;i < n;++i) {
		// printf("%4d:  C= %d    (%02X)\n", i, ls[i].channel, ls[i].type);
		if (ls[i].channel == channelIndex) {
			++sum;
		}
	}
	
	return sum;
}

static int event_sorter_proc(const void *a, const void *b) {
	return (int)( ((MLFEvent_t*)a)->absoluteTicks ) - (int)( ((MLFEvent_t*)b)->absoluteTicks );
}

static void correct_0delta_noteon(MLFEvent_t* ls, int len) {
    MLFEvent_t* prevNoteEv = NULL;
    for (int i = 0;i < (len-1);++i) {
        MLFEvent_t* ev = &ls[i];
        
        if (prevNoteEv && ev) {
            const MLFEventType t1 = prevNoteEv->type;
            const MLFEventType t2 = ev->type;

            if (t1 == ME_NoteOff && t2 == ME_NoteOn) {
                if (prevNoteEv->absoluteTicks == ev->absoluteTicks) {
                    prevNoteEv->absoluteTicks -= 2;
                }
            }
        }
        
        if (ev->type == ME_NoteOn || ev->type == ME_NoteOff) {
            prevNoteEv = ev;
        }
    }
}

MLFEvent_t* lsg_mlf_create_sorted_channel_events(lsg_mlf_t* p_mlf_t, int channelIndex) {
	MLFEvent_t* sorted_buf = NULL;
	
	const int len = lsg_mlf_count_channel_events(p_mlf_t, channelIndex);

	sorted_buf = (MLFEvent_t*)malloc( sizeof(MLFEvent_t) * len );
	int writePos = 0;

	const int nTracks = p_mlf_t->nTracks;
	for (int i = 0;i < nTracks;++i) {
		MLFTrack_t* tr = &p_mlf_t->tracks_arr[i];
		for (int j = 0;j < tr->nEvents;++j) {
			MLFEvent_t* evs = tr->events_arr;
			if (evs[j].channel == channelIndex) {
				MLFEvent_t* dest = &sorted_buf[writePos++];
				*dest = evs[j];

				if (dest->type == ME_NoteOn && dest->velocity == 0) {
					dest->type = ME_NoteOff;
				}

			}
		}
	}

    correct_0delta_noteon(sorted_buf, len);
	qsort(sorted_buf, len, sizeof(MLFEvent_t), event_sorter_proc);

	return sorted_buf;
}

void lsg_mlf_init_play_setup_struct(MLFPlaySetup_t* pSetup) {
    pSetup->deltaScale = 100;
    pSetup->loopDesc.startTicks = pSetup->loopDesc.endTicks = 0;
    lsg_mlf_init_channel_mapping(pSetup->chmap, kLSGNumOutChannels);
}

void lsg_mlf_destroy_play_setup_struct(MLFPlaySetup_t* pSetup) {
    lsg_mlf_destroy_channel_mapping(pSetup->chmap, kLSGNumOutChannels);
}

void lsg_mlf_init_channel_mapping(MappedMLFChannel_t* ls, int count) {
    for (int i = 0;i < count;++i) {
        ls[i].customNoteTableIndex = 0;
        ls[i].eventsLength = 0;
        ls[i].userData = 0;
        ls[i].sortedEvents = NULL;
        ls[i].bEventsArrayIsStatic = 0;
        
        LSG_ADSR* adsr = &ls[i].defaultADSR;
        adsr->attack_rate = adsr->decay_rate = adsr->sustain_level = adsr->release_rate = adsr->fade_rate = 0;
    }
}

void lsg_mlf_destroy_channel_mapping(MappedMLFChannel_t* ls, int count) {
    for (int i = 0;i < count;++i) {
        if (ls[i].sortedEvents) {
            if (0 == ls[i].bEventsArrayIsStatic) {
                free(ls[i].sortedEvents);
            }
            
            ls[i].eventsLength = 0;
            ls[i].sortedEvents = NULL;
        }
    }
}

int read_1blen_message_get_first(FILE* fp, int* outByte) {
	int len = fgetc(fp);
	for (int i = 0;i < len;++i) {
		const int val = fgetc(fp);
        if (i == 0 && outByte) {
            *outByte = val;
        }
	}
	
	return 1 + len;
}

int read_1blen_message(FILE* fp) {
    return read_1blen_message_get_first(fp, NULL);
}

size_t read_smf_u32(FILE* fp) { return read_smf_be(fp, 4); }
size_t read_smf_u16(FILE* fp) { return read_smf_be(fp, 2); }

size_t read_smf_be(FILE* fp, int nBytes) {
	size_t s = 0;
	for (int i = 0;i < nBytes;++i) {
		s <<= 8;
		s |= fgetc(fp);
	}
	
	return s;
}


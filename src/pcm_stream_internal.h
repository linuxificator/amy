#ifndef __AMY_PCM_STREAM_INTERNAL_H
#define __AMY_PCM_STREAM_INTERNAL_H

#include "amy.h"
#include "pcm_stream.h"

// Internal hooks used only by pcm.c. They return true when the oscillator's
// preset is a registered chunk stream and the normal PCM path must not run.
bool pcm_stream_note_on(uint16_t osc);
bool pcm_stream_note_off(uint16_t osc);
bool pcm_stream_render(SAMPLE *buf, uint16_t osc, SAMPLE *max_value);

bool pcm_stream_preset_registered(uint16_t preset_number);
void pcm_stream_unregister_preset(uint16_t preset_number);
void pcm_stream_unregister_all(void);

#endif

// Appendable zero-copy PCM source for callers that fill buffers asynchronously
// (for example, an SD-card task). Existing ROM, memory and file PCM paths stay
// in pcm.c; this module is selected only for explicitly registered presets.

#include "amy.h"
#include "pcm_stream.h"
#include "pcm_stream_internal.h"

#if defined(__GNUC__) || defined(__clang__)
#define PCM_STREAM_LOAD32(p) __atomic_load_n((p), __ATOMIC_ACQUIRE)
#define PCM_STREAM_STORE32(p, v) __atomic_store_n((p), (v), __ATOMIC_RELEASE)
#else
// AMY's concurrent embedded targets are GCC/Clang. Keep other host compilers
// buildable; their tests use one thread, so volatile accesses are sufficient.
#define PCM_STREAM_LOAD32(p) (*(p))
#define PCM_STREAM_STORE32(p, v) (*(p) = (v))
#endif

static amy_pcm_stream_t *pcm_streams = NULL;

static amy_pcm_stream_t *stream_for_preset(uint16_t preset_number) {
    amy_pcm_stream_t *s = pcm_streams;
    while (s != NULL) {
        if (s->registered && s->preset_number == preset_number) return s;
        s = s->next;
    }
    return NULL;
}

static amy_pcm_stream_t *stream_for_osc(uint16_t osc) {
    if (osc >= AMY_OSCS || synth[osc] == NULL || AMY_IS_UNSET(synth[osc]->preset)) return NULL;
    return stream_for_preset(synth[osc]->preset);
}

static void cancel_pending(amy_pcm_stream_t *stream) {
    if (stream == NULL || stream->chunks == NULL) return;
    // Prevent new producer submissions before changing ownership states.
    PCM_STREAM_STORE32(&stream->finished, 1);
    for (uint32_t i = 0; i < stream->capacity; ++i) {
        uint32_t state = PCM_STREAM_LOAD32(&stream->chunks[i].state);
        if (state == AMY_PCM_STREAM_CHUNK_READY || state == AMY_PCM_STREAM_CHUNK_PLAYING)
            PCM_STREAM_STORE32(&stream->chunks[i].state, AMY_PCM_STREAM_CHUNK_CANCELLED);
    }
}

void amy_pcm_stream_init(amy_pcm_stream_t *stream,
                         amy_pcm_stream_chunk_t *chunks,
                         uint32_t capacity) {
    if (stream == NULL) return;
    memset(stream, 0, sizeof(*stream));
    stream->chunks = chunks;
    stream->capacity = capacity;
    stream->active_osc = AMY_PCM_STREAM_NO_OSC;
    if (chunks != NULL) {
        for (uint32_t i = 0; i < capacity; ++i) {
            memset(&chunks[i], 0, sizeof(chunks[i]));
            chunks[i].state = AMY_PCM_STREAM_CHUNK_FREE;
        }
    }
}

int amy_pcm_stream_reset(amy_pcm_stream_t *stream) {
    if (stream == NULL || stream->chunks == NULL || stream->capacity == 0) return -1;
    if (PCM_STREAM_LOAD32(&stream->active_osc) != AMY_PCM_STREAM_NO_OSC) return -1;
    for (uint32_t i = 0; i < stream->capacity; ++i) {
        stream->chunks[i].samples = NULL;
        stream->chunks[i].frames = 0;
        stream->chunks[i].sequence = 0;
        PCM_STREAM_STORE32(&stream->chunks[i].state, AMY_PCM_STREAM_CHUNK_FREE);
    }
    PCM_STREAM_STORE32(&stream->write_index, 0);
    PCM_STREAM_STORE32(&stream->read_index, 0);
    PCM_STREAM_STORE32(&stream->chunks_submitted, 0);
    PCM_STREAM_STORE32(&stream->chunks_played, 0);
    PCM_STREAM_STORE32(&stream->underruns, 0);
    PCM_STREAM_STORE32(&stream->finished, 0);
    stream->phase_q16 = 0;
    return 0;
}

int amy_pcm_stream_append(amy_pcm_stream_t *stream,
                          const int16_t *samples,
                          uint32_t frames) {
    if (stream == NULL || stream->chunks == NULL || stream->capacity == 0
        || samples == NULL || frames == 0) return -1;
    if (PCM_STREAM_LOAD32(&stream->finished)) return -3;

    uint32_t slot = PCM_STREAM_LOAD32(&stream->write_index) % stream->capacity;
    amy_pcm_stream_chunk_t *chunk = &stream->chunks[slot];
    uint32_t state = PCM_STREAM_LOAD32(&chunk->state);
    if (state == AMY_PCM_STREAM_CHUNK_READY || state == AMY_PCM_STREAM_CHUNK_PLAYING)
        return -2;

    uint32_t sequence = PCM_STREAM_LOAD32(&stream->chunks_submitted) + 1;
    chunk->samples = samples;
    chunk->frames = frames;
    chunk->sequence = sequence;
    // Publish READY last: the acquire load in the renderer then sees pointer,
    // frame count and sequence as one complete descriptor.
    PCM_STREAM_STORE32(&chunk->state, AMY_PCM_STREAM_CHUNK_READY);
    PCM_STREAM_STORE32(&stream->chunks_submitted, sequence);
    PCM_STREAM_STORE32(&stream->write_index, (slot + 1) % stream->capacity);
    return (int)slot;
}

void amy_pcm_stream_finish(amy_pcm_stream_t *stream) {
    if (stream != NULL) PCM_STREAM_STORE32(&stream->finished, 1);
}

uint32_t amy_pcm_stream_chunk_state(const amy_pcm_stream_t *stream,
                                    uint32_t slot) {
    if (stream == NULL || stream->chunks == NULL || slot >= stream->capacity)
        return AMY_PCM_STREAM_CHUNK_FREE;
    return PCM_STREAM_LOAD32(&stream->chunks[slot].state);
}

bool pcm_stream_preset_registered(uint16_t preset_number) {
    return stream_for_preset(preset_number) != NULL;
}

void pcm_stream_unregister_preset(uint16_t preset_number) {
    amy_pcm_stream_t **p = &pcm_streams;
    while (*p != NULL) {
        amy_pcm_stream_t *s = *p;
        if (s->registered && s->preset_number == preset_number) {
            *p = s->next;
            cancel_pending(s);
            PCM_STREAM_STORE32(&s->active_osc, AMY_PCM_STREAM_NO_OSC);
            s->registered = 0;
            s->next = NULL;
            return;
        }
        p = &s->next;
    }
}

void pcm_stream_unregister_all(void) {
    amy_pcm_stream_t *s = pcm_streams;
    pcm_streams = NULL;
    while (s != NULL) {
        amy_pcm_stream_t *next = s->next;
        cancel_pending(s);
        PCM_STREAM_STORE32(&s->active_osc, AMY_PCM_STREAM_NO_OSC);
        s->registered = 0;
        s->next = NULL;
        s = next;
    }
}

int pcm_load_stream(uint16_t preset_number,
                    amy_pcm_stream_t *stream,
                    uint32_t samplerate,
                    uint8_t channels,
                    float midinote) {
    if (stream == NULL || stream->chunks == NULL || stream->capacity == 0
        || samplerate == 0 || (channels != 1 && channels != 2)) return -1;

    // Preserve normal preset shadowing semantics: loading a stream replaces
    // an existing memory/file/stream preset with the same number.
    pcm_unload_preset(preset_number);
    if (stream->registered) pcm_stream_unregister_preset(stream->preset_number);

    stream->preset_number = preset_number;
    stream->samplerate = samplerate;
    stream->channels = channels;
    stream->midinote = midinote;
    stream->registered = 1;
    stream->next = pcm_streams;
    pcm_streams = stream;
    return 0;
}

static inline LUTSAMPLE stream_read_frame(const amy_pcm_stream_t *stream,
                                          const amy_pcm_stream_chunk_t *chunk,
                                          uint16_t wave,
                                          uint32_t frame) {
    if (stream->channels == 2) {
        uint32_t off = frame * 2;
        if (wave == PCM_LEFT) return chunk->samples[off];
        if (wave == PCM_RIGHT) return chunk->samples[off + 1];
        return (LUTSAMPLE)(((int32_t)chunk->samples[off]
                          + (int32_t)chunk->samples[off + 1]) / 2);
    }
    return chunk->samples[frame];
}

static void mark_current_done(amy_pcm_stream_t *stream,
                              amy_pcm_stream_chunk_t *chunk,
                              uint32_t slot) {
    PCM_STREAM_STORE32(&chunk->state, AMY_PCM_STREAM_CHUNK_DONE);
    PCM_STREAM_STORE32(&stream->chunks_played,
                       PCM_STREAM_LOAD32(&stream->chunks_played) + 1);
    PCM_STREAM_STORE32(&stream->read_index, (slot + 1) % stream->capacity);
}

// Resolve the descriptor containing stream->phase_q16. Crossing a descriptor
// publishes DONE before advancing, so the producer can immediately recycle a
// ping-pong buffer. The residual phase is carried into the next descriptor,
// which also makes pitch ratios >1 skip frames correctly across boundaries.
static amy_pcm_stream_chunk_t *current_chunk(amy_pcm_stream_t *stream) {
    for (uint32_t n = 0; n <= stream->capacity; ++n) {
        uint32_t slot = PCM_STREAM_LOAD32(&stream->read_index) % stream->capacity;
        amy_pcm_stream_chunk_t *chunk = &stream->chunks[slot];
        uint32_t state = PCM_STREAM_LOAD32(&chunk->state);
        if (state == AMY_PCM_STREAM_CHUNK_READY) {
            PCM_STREAM_STORE32(&chunk->state, AMY_PCM_STREAM_CHUNK_PLAYING);
            state = AMY_PCM_STREAM_CHUNK_PLAYING;
        }
        if (state != AMY_PCM_STREAM_CHUNK_PLAYING) return NULL;

        uint64_t end_q16 = ((uint64_t)chunk->frames) << 16;
        if (stream->phase_q16 < end_q16) return chunk;

        stream->phase_q16 -= end_q16;
        mark_current_done(stream, chunk, slot);
    }
    return NULL;
}

static LUTSAMPLE next_frame_or_hold(amy_pcm_stream_t *stream,
                                    amy_pcm_stream_chunk_t *chunk,
                                    uint16_t wave,
                                    uint32_t base_frame,
                                    LUTSAMPLE hold) {
    if (base_frame + 1 < chunk->frames)
        return stream_read_frame(stream, chunk, wave, base_frame + 1);

    uint32_t slot = PCM_STREAM_LOAD32(&stream->read_index) % stream->capacity;
    uint32_t next_slot = (slot + 1) % stream->capacity;
    amy_pcm_stream_chunk_t *next = &stream->chunks[next_slot];
    if (PCM_STREAM_LOAD32(&next->state) == AMY_PCM_STREAM_CHUNK_READY
        && next->frames > 0 && next->samples != NULL)
        return stream_read_frame(stream, next, wave, 0);
    return hold;
}

static void stop_stream_osc(amy_pcm_stream_t *stream, uint16_t osc, bool cancel) {
    if (cancel) cancel_pending(stream);
    synth[osc]->status = SYNTH_OFF;
    PCM_STREAM_STORE32(&stream->active_osc, AMY_PCM_STREAM_NO_OSC);
}

bool pcm_stream_note_on(uint16_t osc) {
    amy_pcm_stream_t *stream = stream_for_osc(osc);
    if (stream == NULL) return false;

    uint32_t active = PCM_STREAM_LOAD32(&stream->active_osc);
    if (active != AMY_PCM_STREAM_NO_OSC && active != osc) {
        fprintf(stderr, "amy: PCM stream preset %u is already consumed by osc %u; "
                        "use a separate stream/preset for polyphony\n",
                stream->preset_number, (unsigned)active);
        synth[osc]->status = SYNTH_OFF;
        return true;
    }

    PCM_STREAM_STORE32(&stream->active_osc, osc);
    stream->phase_q16 = 0;
    synth[osc]->phase = 0;
    synth[osc]->stretch.active = 0;  // sequential sources cannot random-access grains
    msynth[osc]->loopstart = 0;
    msynth[osc]->loopend = 0;
    msynth[osc]->state = synth[osc]->mode;
    msynth[osc]->pcm_delay = 0;
    if (AMY_IS_SET(synth[osc]->sample_offset))
        msynth[osc]->pcm_delay = synth[osc]->sample_offset % AMY_BLOCK_SIZE;
    synth[osc]->terminate_on_silence = 0;

    if (AMY_IS_SET(synth[osc]->fit_ticks))
        fprintf(stderr, "amy: fit is not supported on appendable PCM streams; ignoring fit\n");
    if (AMY_IS_SET(synth[osc]->trigger_phase) && synth[osc]->trigger_phase != 0)
        fprintf(stderr, "amy: trigger_phase is not supported on appendable PCM streams; starting at 0\n");
    return true;
}

bool pcm_stream_note_off(uint16_t osc) {
    amy_pcm_stream_t *stream = stream_for_osc(osc);
    if (stream == NULL) return false;

    if (msynth[osc]->state == PCM_PLAY_STOP || msynth[osc]->state == PCM_LOOP_STOP) {
        stop_stream_osc(stream, osc, true);
    } else if (msynth[osc]->state == PCM_LOOP_FOREVER) {
        msynth[osc]->state = PCM_LOOP;
        synth[osc]->terminate_on_silence = 1;
    } else if (msynth[osc]->state == PCM_LOOP || msynth[osc]->state == PCM_PLAY) {
        msynth[osc]->state = PCM_PLAY_STOP;
    }
    return true;
}

bool pcm_stream_render(SAMPLE *buf, uint16_t osc, SAMPLE *max_value_out) {
    amy_pcm_stream_t *stream = stream_for_osc(osc);
    if (stream == NULL) return false;

    SAMPLE max_value = 0;
    if (max_value_out != NULL) *max_value_out = 0;
    if (PCM_STREAM_LOAD32(&stream->active_osc) != osc) return true;

    float logfreq = msynth[osc]->logfreq;
    if (AMY_IS_SET(synth[osc]->midi_note))
        logfreq -= logfreq_for_midi_note(stream->midinote);
    float log2sr = log2f((float)stream->samplerate / ZERO_LOGFREQ_IN_HZ);
    float playback_freq = freq_of_logfreq(log2sr + logfreq);
    if (logfreq == 0 && AMY_IS_UNSET(synth[osc]->midi_note))
        playback_freq = (float)stream->samplerate;
    uint32_t step_q16 = (uint32_t)((playback_freq / (float)AMY_SAMPLE_RATE) * 65536.0f);
    if (step_q16 == 0) step_q16 = 1;

    SAMPLE amp = F2S(msynth[osc]->amp);
    uint16_t i = 0;
    if (msynth[osc]->pcm_delay) {
        i = msynth[osc]->pcm_delay;
        msynth[osc]->pcm_delay = 0;
    }
    bool starved = false;
    for (; i < AMY_BLOCK_SIZE; ++i) {
        amy_pcm_stream_chunk_t *chunk = current_chunk(stream);
        if (chunk == NULL) {
            if (PCM_STREAM_LOAD32(&stream->finished))
                stop_stream_osc(stream, osc, false);
            else
                starved = true;
            break;
        }

        uint32_t base_frame = (uint32_t)(stream->phase_q16 >> 16);
        uint32_t frac_q16 = (uint32_t)(stream->phase_q16 & 0xffffu);
        LUTSAMPLE b = stream_read_frame(stream, chunk, synth[osc]->wave, base_frame);
        LUTSAMPLE c = next_frame_or_hold(stream, chunk, synth[osc]->wave, base_frame, b);
        SAMPLE frac = (SAMPLE)frac_q16 << (S_FRAC_BITS - 16);
        SAMPLE sample = L2S(b) + MUL4_SS(L2S(c - b), frac);
        SAMPLE value = buf[i] + MUL4_SS(amp, sample);
        buf[i] = value;
        if (value < 0) value = -value;
        if (value > max_value) max_value = value;
        stream->phase_q16 += step_q16;
    }

    // Publish a chunk that ended exactly on the final output frame immediately;
    // otherwise a ping-pong producer would wait an unnecessary whole block.
    amy_pcm_stream_chunk_t *after = current_chunk(stream);
    if (after == NULL && PCM_STREAM_LOAD32(&stream->finished))
        stop_stream_osc(stream, osc, false);
    if (starved)
        PCM_STREAM_STORE32(&stream->underruns,
                           PCM_STREAM_LOAD32(&stream->underruns) + 1);

    if (max_value_out != NULL) *max_value_out = max_value;
    return true;
}

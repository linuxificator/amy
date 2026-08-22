#ifndef __AMY_PCM_STREAM_H
#define __AMY_PCM_STREAM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// A PCM chunk stream is a caller-owned, single-producer/single-consumer ring.
// The producer (for example an SD-card task) supplies already-filled memory
// regions. AMY consumes them in order without allocating or copying them.
//
// Chunk state is the ownership contract for ping-pong buffers:
//   FREE       descriptor has never been queued / was reset
//   READY      producer published it; AMY may read the sample memory
//   PLAYING    AMY is currently reading the sample memory
//   DONE       AMY has passed the end; sample memory is safe to overwrite
//   CANCELLED  playback stopped before this chunk was consumed; also safe
//              to overwrite
//
// The producer must not modify samples while a descriptor is READY or
// PLAYING. amy_pcm_stream_append() only reuses FREE, DONE or CANCELLED slots.
#define AMY_PCM_STREAM_CHUNK_FREE       0u
#define AMY_PCM_STREAM_CHUNK_READY      1u
#define AMY_PCM_STREAM_CHUNK_PLAYING    2u
#define AMY_PCM_STREAM_CHUNK_DONE       3u
#define AMY_PCM_STREAM_CHUNK_CANCELLED  4u

#define AMY_PCM_STREAM_NO_OSC UINT32_MAX

typedef struct {
    const int16_t *samples;       // interleaved when channels == 2
    uint32_t frames;              // frames, not int16 sample count
    volatile uint32_t state;      // AMY_PCM_STREAM_CHUNK_*
    uint32_t sequence;            // monotonically increasing submission id
} amy_pcm_stream_chunk_t;

typedef struct amy_pcm_stream_t {
    amy_pcm_stream_chunk_t *chunks;
    uint32_t capacity;

    // Producer / consumer progress. These are deliberately visible so an
    // embedded host can inspect progress without a callback or allocation.
    volatile uint32_t write_index;
    volatile uint32_t read_index;
    volatile uint32_t chunks_submitted;
    volatile uint32_t chunks_played;
    volatile uint32_t underruns;
    volatile uint32_t finished;
    volatile uint32_t active_osc;

    // Playback metadata for the preset registered with pcm_load_stream().
    uint16_t preset_number;
    uint8_t channels;
    uint8_t registered;
    uint32_t samplerate;
    float midinote;

    // Consumer-only state. Hosts should treat these fields as opaque.
    uint64_t phase_q16;
    struct amy_pcm_stream_t *next;
} amy_pcm_stream_t;

// Initialize caller-owned stream + descriptor storage. No heap allocation.
// capacity may be 1, though >=2 is useful for ping-pong streaming.
void amy_pcm_stream_init(amy_pcm_stream_t *stream,
                         amy_pcm_stream_chunk_t *chunks,
                         uint32_t capacity);

// Reset descriptors and counters for a new playback. Returns -1 if an
// oscillator is still actively consuming this stream.
int amy_pcm_stream_reset(amy_pcm_stream_t *stream);

// Append one memory region. Returns the descriptor slot index, or:
//   -1 invalid arguments
//   -2 ring full (next descriptor is still READY/PLAYING)
//   -3 stream has been marked finished; reset before appending again
int amy_pcm_stream_append(amy_pcm_stream_t *stream,
                          const int16_t *samples,
                          uint32_t frames);

// Tell the consumer that no chunks will follow. Playback stops after the
// queued data drains. Without finish(), an empty ring is an underrun/wait:
// the oscillator remains alive so the producer can append later.
void amy_pcm_stream_finish(amy_pcm_stream_t *stream);

// Acquire-safe state read for producer code. Invalid slot -> FREE.
uint32_t amy_pcm_stream_chunk_state(const amy_pcm_stream_t *stream,
                                    uint32_t slot);

// Register this stream as a PCM preset. The stream memory remains caller-owned.
// This shadows a ROM/memory/file preset with the same number until unloaded.
// Returns 0 on success, -1 for invalid arguments.
int pcm_load_stream(uint16_t preset_number,
                    amy_pcm_stream_t *stream,
                    uint32_t samplerate,
                    uint8_t channels,
                    float midinote);

#ifdef __cplusplus
}
#endif

#endif

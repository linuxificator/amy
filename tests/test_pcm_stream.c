// Regression tests for appendable zero-copy PCM streams.
// Build/run with `make tests/test_pcm_stream` or `make ctest`.

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "amy.h"
#include "pcm_stream.h"

static int failures = 0;

#define CHECK(cond, fmt, ...) do {                                         \
    if (cond) { printf("  ok   " fmt "\n", ##__VA_ARGS__); }             \
    else { printf("  FAIL " fmt "\n", ##__VA_ARGS__); failures++; }      \
} while (0)

#define STREAM_PRESET_A 60000
#define STREAM_PRESET_B 60001
#define STREAM_PRESET_C 60002
#define MEMORY_PRESET   60003

static int16_t a[AMY_BLOCK_SIZE];
static int16_t b[AMY_BLOCK_SIZE];
static int16_t c[AMY_BLOCK_SIZE];
static int16_t short_chunk[AMY_BLOCK_SIZE / 2];

void delay_ms(uint32_t ms) { (void)ms; }

static void fill_buffers(void) {
    for (uint32_t i = 0; i < AMY_BLOCK_SIZE; ++i) {
        a[i] = 1000;
        b[i] = 2000;
        c[i] = 3000;
    }
    for (uint32_t i = 0; i < AMY_BLOCK_SIZE / 2; ++i)
        short_chunk[i] = 4000;
}

static void render_blocks(int n) {
    for (int i = 0; i < n; ++i) amy_simple_fill_buffer();
}

static void start_raw_pcm(uint16_t osc, uint16_t preset) {
    char msg[80];
    snprintf(msg, sizeof(msg), "v%uw%dp%ul1Z", osc, PCM, preset);
    amy_add_message(msg);
    render_blocks(1);
}

static void stop_raw_pcm(uint16_t osc) {
    char msg[32];
    snprintf(msg, sizeof(msg), "v%ul0Z", osc);
    amy_add_message(msg);
    render_blocks(1);
}

static void test_ring_ownership(void) {
    printf("descriptor ring ownership\n");
    amy_pcm_stream_t stream;
    amy_pcm_stream_chunk_t chunks[2];
    amy_pcm_stream_init(&stream, chunks, 2);

    CHECK(amy_pcm_stream_chunk_state(&stream, 0) == AMY_PCM_STREAM_CHUNK_FREE,
          "slot 0 starts FREE");
    CHECK(amy_pcm_stream_chunk_state(&stream, 1) == AMY_PCM_STREAM_CHUNK_FREE,
          "slot 1 starts FREE");
    CHECK(amy_pcm_stream_append(&stream, a, AMY_BLOCK_SIZE) == 0,
          "first append publishes slot 0");
    CHECK(amy_pcm_stream_append(&stream, b, AMY_BLOCK_SIZE) == 1,
          "second append publishes slot 1");
    CHECK(amy_pcm_stream_append(&stream, c, AMY_BLOCK_SIZE) == -2,
          "third append refuses to overwrite READY data");
    CHECK(chunks[0].sequence == 1 && chunks[1].sequence == 2,
          "submission sequence is monotonic");
    amy_pcm_stream_finish(&stream);
    CHECK(amy_pcm_stream_append(&stream, c, AMY_BLOCK_SIZE) == -3,
          "append after finish requires reset");
}

static void test_ping_pong_reuse(void) {
    printf("played chunks become reusable while playback continues\n");
    amy_pcm_stream_t stream;
    amy_pcm_stream_chunk_t chunks[2];
    amy_pcm_stream_init(&stream, chunks, 2);
    CHECK(pcm_load_stream(STREAM_PRESET_A, &stream, AMY_SAMPLE_RATE, 1, 60.0f) == 0,
          "stream preset registers");
    CHECK(amy_pcm_stream_append(&stream, a, AMY_BLOCK_SIZE) == 0,
          "queue buffer A");
    CHECK(amy_pcm_stream_append(&stream, b, AMY_BLOCK_SIZE) == 1,
          "queue buffer B");

    start_raw_pcm(0, STREAM_PRESET_A);
    for (int i = 0; i < 4 && amy_pcm_stream_chunk_state(&stream, 0) != AMY_PCM_STREAM_CHUNK_DONE; ++i)
        render_blocks(1);

    CHECK(amy_pcm_stream_chunk_state(&stream, 0) == AMY_PCM_STREAM_CHUNK_DONE,
          "buffer A is DONE after AMY passes it");
    CHECK(amy_pcm_stream_chunk_state(&stream, 1) == AMY_PCM_STREAM_CHUNK_READY
          || amy_pcm_stream_chunk_state(&stream, 1) == AMY_PCM_STREAM_CHUNK_PLAYING,
          "buffer B is still owned by AMY");
    CHECK(amy_pcm_stream_append(&stream, c, AMY_BLOCK_SIZE) == 0,
          "producer can immediately refill/requeue buffer A's slot");
    CHECK(chunks[0].sequence == 3,
          "reused slot receives the next sequence number");

    amy_pcm_stream_finish(&stream);
    for (int i = 0; i < 8 && stream.active_osc != AMY_PCM_STREAM_NO_OSC; ++i)
        render_blocks(1);
    CHECK(stream.active_osc == AMY_PCM_STREAM_NO_OSC,
          "finished stream stops after all queued chunks drain");
    CHECK(stream.chunks_played == 3,
          "all three chunks were consumed exactly once");
    pcm_unload_preset(STREAM_PRESET_A);
}

static void test_underrun_resume(void) {
    printf("an unfinished empty queue waits and resumes\n");
    amy_pcm_stream_t stream;
    amy_pcm_stream_chunk_t chunks[2];
    amy_pcm_stream_init(&stream, chunks, 2);
    CHECK(pcm_load_stream(STREAM_PRESET_B, &stream, AMY_SAMPLE_RATE, 1, 60.0f) == 0,
          "stream preset registers");
    CHECK(amy_pcm_stream_append(&stream, short_chunk, AMY_BLOCK_SIZE / 2) == 0,
          "queue first short chunk");

    start_raw_pcm(1, STREAM_PRESET_B);
    render_blocks(2);
    CHECK(amy_pcm_stream_chunk_state(&stream, 0) == AMY_PCM_STREAM_CHUNK_DONE,
          "first short chunk is released");
    CHECK(stream.underruns > 0,
          "empty unfinished stream records an underrun");
    CHECK(stream.active_osc == 1,
          "underrun does not kill the oscillator");

    CHECK(amy_pcm_stream_append(&stream, b, AMY_BLOCK_SIZE) == 1,
          "new data may be appended after the underrun");
    amy_pcm_stream_finish(&stream);
    for (int i = 0; i < 8 && stream.active_osc != AMY_PCM_STREAM_NO_OSC; ++i)
        render_blocks(1);
    CHECK(amy_pcm_stream_chunk_state(&stream, 1) == AMY_PCM_STREAM_CHUNK_DONE,
          "appended data is consumed after resume");
    CHECK(stream.active_osc == AMY_PCM_STREAM_NO_OSC,
          "finished resumed stream stops normally");
    pcm_unload_preset(STREAM_PRESET_B);
}

static void test_note_off_releases_buffers(void) {
    printf("note-off cancels buffers that AMY still owns\n");
    amy_pcm_stream_t stream;
    amy_pcm_stream_chunk_t chunks[2];
    amy_pcm_stream_init(&stream, chunks, 2);
    CHECK(pcm_load_stream(STREAM_PRESET_C, &stream, AMY_SAMPLE_RATE, 1, 60.0f) == 0,
          "stream preset registers");
    CHECK(amy_pcm_stream_append(&stream, a, AMY_BLOCK_SIZE) == 0,
          "queue buffer A");
    CHECK(amy_pcm_stream_append(&stream, b, AMY_BLOCK_SIZE) == 1,
          "queue buffer B");

    start_raw_pcm(2, STREAM_PRESET_C);
    stop_raw_pcm(2);
    uint32_t s0 = amy_pcm_stream_chunk_state(&stream, 0);
    uint32_t s1 = amy_pcm_stream_chunk_state(&stream, 1);
    CHECK(s0 == AMY_PCM_STREAM_CHUNK_DONE || s0 == AMY_PCM_STREAM_CHUNK_CANCELLED,
          "first slot is safe after stop");
    CHECK(s1 == AMY_PCM_STREAM_CHUNK_DONE || s1 == AMY_PCM_STREAM_CHUNK_CANCELLED,
          "second slot is safe after stop");
    CHECK(stream.active_osc == AMY_PCM_STREAM_NO_OSC,
          "note-off relinquishes stream ownership");
    pcm_unload_preset(STREAM_PRESET_C);
}

static void test_contiguous_pcm_unchanged(void) {
    printf("existing contiguous memory PCM stays on its original path\n");
    int16_t *ram = pcm_load(MEMORY_PRESET, 32, AMY_SAMPLE_RATE, 1, 60.0f, 0, 0);
    CHECK(ram != NULL, "pcm_load still allocates one contiguous sample");
    if (ram != NULL) {
        for (int i = 0; i < 32; ++i) ram[i] = (int16_t)(i * 100);
        uint32_t length = 0;
        const int16_t *readback = pcm_get_sample_ram_for_preset(MEMORY_PRESET, &length);
        CHECK(readback == ram, "contiguous preset exposes the same memory pointer");
        CHECK(length == 32, "contiguous preset length is unchanged");
    }
    pcm_unload_preset(MEMORY_PRESET);

    amy_pcm_stream_t stream;
    amy_pcm_stream_chunk_t chunks[1];
    amy_pcm_stream_init(&stream, chunks, 1);
    CHECK(pcm_load_stream(MEMORY_PRESET, &stream, AMY_SAMPLE_RATE, 1, 60.0f) == 0,
          "stream can use the same preset namespace");
    uint32_t length = 123;
    CHECK(pcm_get_sample_ram_for_preset(MEMORY_PRESET, &length) == NULL,
          "stream preset never masquerades as contiguous RAM");
    CHECK(length == 0, "stream contiguous-length readback is zero");
    pcm_unload_preset(MEMORY_PRESET);
}

int main(void) {
    fill_buffers();
    amy_config_t config = amy_default_config();
    config.features.startup_bleep = 0;
    config.features.default_synths = 0;
    amy_start(config);
    render_blocks(2);

    test_ring_ownership();
    test_ping_pong_reuse();
    test_underrun_resume();
    test_note_off_releases_buffers();
    test_contiguous_pcm_unchanged();

    amy_stop();
    if (failures) {
        printf("%d FAILURES\n", failures);
        return 1;
    }
    printf("all ok\n");
    return 0;
}

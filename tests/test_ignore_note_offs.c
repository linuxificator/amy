// Regression coverage for synths which intentionally ignore note-offs.
//
// A one-shot drum synth can receive an unlimited series of note-ons without
// matching note-offs.  Voice stealing must not put those notes into the
// bounded forgotten-note pool: its only purpose is to absorb note-offs which
// this synth has explicitly said will be ignored.

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "amy.h"

static int failures = 0;

#define CHECK(cond, message) do {                         \
    if (cond) { printf("  ok   %s\n", message); }         \
    else { printf("  FAIL %s\n", message); failures++; } \
} while (0)

static void render_a_bit(void) {
    for (int i = 0; i < 4; ++i) amy_simple_fill_buffer();
}

static void send(const char *message) {
    amy_add_message((char *)message);
    render_a_bit();
}

static int file_contains(const char *path, const char *needle) {
    FILE *file = fopen(path, "r");
    if (file == NULL) return 0;
    char buffer[8192] = {0};
    size_t count = fread(buffer, 1, sizeof(buffer) - 1, file);
    buffer[count] = 0;
    fclose(file);
    return strstr(buffer, needle) != NULL;
}

static void test_ignored_note_offs_do_not_fill_forgotten_pool(void) {
    const char *path = "test_ignore_note_offs.stderr.tmp";
    printf("ignored note-offs require no forgotten-note bookkeeping\n");
    fflush(stderr);
    FILE *redirected = freopen(path, "w", stderr);

    // This is the shape used by a small polyphonic one-shot PCM drum synth:
    // four voices, one oscillator per voice, and no note-offs by design.
    send("i0iv4in1if2Z");
    for (int note = 1; note <= 64; ++note) {
        char message[32];
        snprintf(message, sizeof(message), "n%dl1i0Z", note);
        send(message);
    }

    fflush(stderr);
    int overflow = redirected != NULL
        && file_contains(path, "forgotten pool overflow");
    FILE *restored = freopen("/dev/stderr", "w", stderr);
    (void)restored;
    remove(path);
    CHECK(!overflow, "64 one-shot onsets do not overflow the pool");
}

// examples.o wants this from amy-example.c; every ctest stubs it.
void delay_ms(uint32_t ms) { (void)ms; }

int main(void) {
    amy_config_t config = amy_default_config();
    config.features.startup_bleep = 0;
    amy_start(config);
    render_a_bit();

    test_ignored_note_offs_do_not_fill_forgotten_pool();

    amy_stop();
    if (failures) { printf("%d FAILURES\n", failures); return 1; }
    printf("all ok\n");
    return 0;
}

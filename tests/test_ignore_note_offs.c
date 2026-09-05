// Regression coverage for synths which intentionally ignore note-offs.
//
// A one-shot drum synth can receive an unlimited series of note-ons without
// matching note-offs.  Voice stealing must not put those notes into the
// bounded forgotten-note pool: its only purpose is to absorb note-offs which
// this synth has explicitly said will be ignored.

#include <stdio.h>
#include <stdint.h>
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

extern int instrument_test_forgotten_note_slots(int instrument_number);

static void test_ignored_note_offs_do_not_fill_forgotten_pool(void) {
    printf("ignored note-offs require no forgotten-note bookkeeping\n");
    // This is the shape used by a small polyphonic one-shot PCM drum synth:
    // four voices, one oscillator per voice, and no note-offs by design.
    send("i0iv4in1if2Z");
    for (int note = 1; note <= 64; ++note) {
        char message[32];
        snprintf(message, sizeof(message), "n%dl1i0Z", note);
        send(message);
    }

    CHECK(instrument_test_forgotten_note_slots(0) == 0,
          "64 one-shot onsets leave the pool empty");
}

static void test_ordinary_synths_still_track_stolen_notes(void) {
    printf("ordinary synths retain forgotten-note matching\n");
    send("i1iv4in1Z");
    for (int note = 1; note <= 5; ++note) {
        char message[32];
        snprintf(message, sizeof(message), "n%dl1i1Z", note);
        send(message);
    }
    CHECK(instrument_test_forgotten_note_slots(1) == 1,
          "one stolen ordinary note occupies one pool slot");
}

// examples.o wants this from amy-example.c; every ctest stubs it.
void delay_ms(uint32_t ms) { (void)ms; }

int main(void) {
    amy_config_t config = amy_default_config();
    config.features.startup_bleep = 0;
    amy_start(config);
    render_a_bit();

    test_ignored_note_offs_do_not_fill_forgotten_pool();
    test_ordinary_synths_still_track_stolen_notes();

    amy_stop();
    if (failures) { printf("%d FAILURES\n", failures); return 1; }
    printf("all ok\n");
    return 0;
}

// Shared aux-reverb routing, fixed arenas, and deferred diagnostics.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "amy.h"

#define ROOM_BYTES (128u * 1024u)

static int failures;
static uint8_t room_memory[2][ROOM_BYTES];
static void *room_arenas[2] = { room_memory[0], room_memory[1] };

#define CHECK(c, fmt, ...) do {                                           \
    if (c) printf("  ok   " fmt "\n", ##__VA_ARGS__);                    \
    else { printf("  FAIL " fmt "\n", ##__VA_ARGS__); ++failures; }     \
} while (0)

static bool inside_room(const void *pointer, int room) {
    uintptr_t p = (uintptr_t)pointer;
    uintptr_t first = (uintptr_t)room_memory[room];
    return p >= first && p < first + ROOM_BYTES;
}

static void start_shared(void) {
    amy_stop();
    amy_config_t config = amy_default_config();
    config.features.startup_bleep = 0;
    config.max_buses = 4;
    config.max_reverb_rooms = 2;
    config.reverb_room_memory = room_arenas;
    config.reverb_room_memory_bytes = ROOM_BYTES;
    config.reverb_diagnostics = 1;
    amy_start(config);
}

static void test_arena_and_wire_routing(void) {
    puts("fixed rooms and hR/hS routing");
    start_shared();
    for (int room = 0; room < 2; ++room) {
        shared_reverb_state_t *state = &amy_global.reverb_rooms[room];
        CHECK(state->arena == room_memory[room], "room %d uses its arena", room);
        CHECK(state->arena_used > 108u * 1024u && state->arena_used < ROOM_BYTES,
              "room %d fits (%zu/%u bytes)", room, state->arena_used, ROOM_BYTES);
        CHECK(inside_room(state->effect.rev, room), "room %d state is contained", room);
        CHECK(inside_room(state->block, room), "room %d workspace is contained", room);
        CHECK(inside_room(state->effect.rev->delay_1->samples, room),
              "room %d delay data is contained", room);
    }

    amy_add_message("hR0,0.6,0.8,0.4,2800Z");
    amy_add_message("hR1,0.3,0.7,0.2,3500Z");
    amy_add_message("y2hS1,0.75Z");
    amy_execute_deltas();
    CHECK(S2F(amy_global.reverb_rooms[0].effect.level) > 0.59f,
          "room 0 level configured");
    CHECK(amy_global.reverb_rooms[1].effect.xover_hz == 3500.0f,
          "room 1 filter configured");
    CHECK(amy_global.bus[2]->reverb_send_room == 1, "bus 2 targets room 1");
    CHECK(S2F(amy_global.bus[2]->reverb_send_level) > 0.74f,
          "bus 2 has a weighted send");
    amy_add_message("y2hS1,0Z");
    amy_execute_deltas();
    CHECK(amy_global.bus[2]->reverb_send_level == 0,
          "zero send excludes a bus without changing its room");
}

static void test_audio_and_deferred_diagnostics(void) {
    puts("audio path and stored diagnostics");
    start_shared();

    // Configured storage is cheap while its return level is disabled: it must
    // not walk the delay lines merely because a room exists.
    for (int i = 0; i < 2; ++i) amy_simple_fill_buffer();
    amy_reverb_diagnostic_t room, stage;
    CHECK(amy_reverb_diagnostics_get(0, &room), "disabled-room snapshot succeeds");
    CHECK(room.calls == 0, "disabled room performs no DSP work");

    amy_add_message("hR0,0.8,0.85,0.5,3000Zy0hS0,1Zv0w0n60l1Z");
    for (int i = 0; i < 48; ++i) amy_simple_fill_buffer();

    CHECK(amy_reverb_diagnostics_get(0, &room), "room snapshot succeeds");
    CHECK(amy_reverb_stage_diagnostics_get(&stage), "stage snapshot succeeds");
    CHECK(room.calls == 48, "room measured once per rendered block (%llu)",
          (unsigned long long)room.calls);
    CHECK(stage.calls == 50, "stage measured once per rendered block (%llu)",
          (unsigned long long)stage.calls);
    CHECK(room.core_mask == 1, "host room ran on its one render core");

    bool wet_nonzero = false;
    SAMPLE *wet = amy_global.reverb_rooms[0].block;
    for (int i = 0; i < AMY_BLOCK_SIZE * AMY_NCHANS; ++i)
        if (wet[i] != 0) wet_nonzero = true;
    CHECK(wet_nonzero, "shared room produced a wet return");
}

static void test_legacy_default(void) {
    puts("legacy per-bus behavior remains the default");
    amy_stop();
    amy_config_t config = amy_default_config();
    config.features.startup_bleep = 0;
    amy_start(config);
    CHECK(amy_global.config.max_reverb_rooms == 0, "shared rooms default off");
    amy_add_message("y0h0.5,0.8,0.4,3000Z");
    amy_execute_deltas();
    CHECK(amy_global.bus[0]->reverb.rev != NULL,
          "historical h command still allocates a per-bus reverb");
    CHECK(S2F(amy_global.bus[0]->reverb.level) > 0.49f,
          "historical h level is unchanged");
}

void delay_ms(uint32_t ms) { (void)ms; }

int main(void) {
    amy_config_t config = amy_default_config();
    config.features.startup_bleep = 0;
    amy_start(config);
    test_arena_and_wire_routing();
    test_audio_and_deferred_diagnostics();
    test_legacy_default();
    amy_stop();
    if (failures) return 1;
    puts("all shared reverb checks passed");
    return 0;
}

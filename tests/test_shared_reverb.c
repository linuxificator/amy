// Shared aux-reverb routing, fixed arenas, and deferred diagnostics.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "amy.h"

#define ROOM_BYTES (128u * 1024u)

static int failures;
static uint8_t room_memory[2][ROOM_BYTES];
static void *room_arenas[2] = { room_memory[0], room_memory[1] };
static unsigned bus_hook_calls[4];
static unsigned external_return_calls;
static bool external_return_received_audio;
static uint8_t external_return_selector[2] = { 0, 1 };

#define CHECK(c, fmt, ...) do {                                           \
    if (c) printf("  ok   " fmt "\n", ##__VA_ARGS__);                    \
    else { printf("  FAIL " fmt "\n", ##__VA_ARGS__); ++failures; }     \
} while (0)

static bool inside_room(const void *pointer, int room) {
    uintptr_t p = (uintptr_t)pointer;
    uintptr_t first = (uintptr_t)room_memory[room];
    return p >= first && p < first + ROOM_BYTES;
}

static void count_bus_hook(uint16_t bus, SAMPLE *buf, uint16_t len) {
    (void)buf;
    CHECK(bus < 4, "postprocess hook bus is in range (%u)", bus);
    CHECK(len == AMY_BLOCK_SIZE, "postprocess hook receives one block (%u)", len);
    if (bus < 4) ++bus_hook_calls[bus];
}

static void process_external_return(uint16_t return_index, SAMPLE *block,
                                    uint16_t frames, void *user_data) {
    unsigned *calls = (unsigned *)user_data;
    CHECK(return_index == 1, "external callback receives return index");
    CHECK(frames == AMY_BLOCK_SIZE, "external callback receives one block");
    ++*calls;
    for (int i = 0; i < frames * AMY_NCHANS; ++i) {
        if (block[i] != 0) external_return_received_audio = true;
        block[i] /= 2;
    }
}

static void start_shared_with_hook(bool hook) {
    amy_stop();
    amy_config_t config = amy_default_config();
    config.features.startup_bleep = 0;
    config.max_buses = 4;
    config.max_reverb_rooms = 2;
    config.reverb_room_memory = room_arenas;
    config.reverb_room_memory_bytes = ROOM_BYTES;
    config.reverb_diagnostics = 1;
    config.amy_external_bus_postprocess_hook = hook ? count_bus_hook : NULL;
    amy_start(config);
}

static void start_shared(void) { start_shared_with_hook(false); }

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
    amy_reverb_diagnostic_t room0, room1, stage;
    CHECK(amy_reverb_diagnostics_get(0, &room0), "disabled-room snapshot succeeds");
    CHECK(room0.calls == 0, "disabled room performs no DSP work");

    amy_add_message("hR0,0.8,0.85,0.5,3000Z"
                    "hR1,0.6,0.75,0.4,2600Z"
                    "y0hS0,1Zy1hS1,0.7Z"
                    "v0w0n60l1y0Zv1w0n67l1y1Z");
    for (int i = 0; i < 48; ++i) amy_simple_fill_buffer();

    CHECK(amy_reverb_diagnostics_get(0, &room0), "room 0 snapshot succeeds");
    CHECK(amy_reverb_diagnostics_get(1, &room1), "room 1 snapshot succeeds");
    CHECK(amy_reverb_stage_diagnostics_get(&stage), "stage snapshot succeeds");
    CHECK(room0.calls == 48, "room 0 ran once per rendered block (%llu)",
          (unsigned long long)room0.calls);
    CHECK(room1.calls == 48, "room 1 ran once per rendered block (%llu)",
          (unsigned long long)room1.calls);
    CHECK(stage.calls == 50, "stage measured once per rendered block (%llu)",
          (unsigned long long)stage.calls);
    CHECK(room0.core_mask == 1 && room1.core_mask == 1,
          "both host rooms ran on the host render core");

    for (int room = 0; room < 2; ++room) {
        bool wet_nonzero = false;
        SAMPLE *wet = amy_global.reverb_rooms[room].block;
        for (int i = 0; i < AMY_BLOCK_SIZE * AMY_NCHANS; ++i)
            if (wet[i] != 0) wet_nonzero = true;
        CHECK(wet_nonzero, "shared room %d produced a wet return", room);
    }
}

static void test_external_hook_serial_fallback(void) {
    puts("external bus hooks retain one ordered callback per bus");
    for (int bus = 0; bus < 4; ++bus) bus_hook_calls[bus] = 0;
    start_shared_with_hook(true);
    amy_add_message("y3V1Z");
    amy_execute_deltas();
    amy_simple_fill_buffer();
    for (int bus = 0; bus < 4; ++bus)
        CHECK(bus_hook_calls[bus] == 1, "bus %d hook ran once", bus);
}

static void test_external_aux_return(void) {
    puts("host-selected aux-return effect");
    amy_stop();
    external_return_calls = 0;
    external_return_received_audio = false;
    amy_config_t config = amy_default_config();
    config.features.startup_bleep = 0;
    config.max_buses = 4;
    config.max_reverb_rooms = 2;
    config.reverb_room_memory = room_arenas;
    config.reverb_room_memory_bytes = ROOM_BYTES;
    config.aux_return_external = external_return_selector;
    config.amy_external_aux_return_process_hook = process_external_return;
    config.amy_external_aux_return_user_data = &external_return_calls;
    amy_start(config);

    CHECK(amy_global.allocated_reverbs == 1,
          "only the built-in return allocates a reverb");
    CHECK(amy_global.reverb_rooms[0].effect.rev != NULL,
          "return 0 uses AMY's built-in effect");
    CHECK(amy_global.reverb_rooms[1].external_effect,
          "return 1 is host processed");
    CHECK(amy_global.reverb_rooms[1].effect.rev == NULL,
          "external return allocates no built-in reverb");
    CHECK(amy_global.reverb_rooms[1].arena_used
              == sizeof(SAMPLE) * AMY_BLOCK_SIZE * AMY_NCHANS,
          "external return arena contains only its block");

    amy_add_message("y2hS1,1Zv0w0n60l1y2Z");
    amy_execute_deltas();
    for (int i = 0; i < 4; ++i) amy_simple_fill_buffer();
    CHECK(external_return_calls == 4,
          "external effect ran once per block (%u)", external_return_calls);
    CHECK(external_return_received_audio,
          "external effect received the selected bus audio");
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
    test_external_hook_serial_fallback();
    test_external_aux_return();
    test_legacy_default();
    amy_stop();
    if (failures) return 1;
    puts("all shared reverb checks passed");
    return 0;
}

#include "bus_mixer.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

/* AMY's core-0 bus buffers are combined before the per-bus FX pass. */
extern SAMPLE **fbl[AMY_MAX_CORES];
extern global_state_t amy_global;

/*
 * Control values are stored as unsigned Q0.16-like fractions (0..65535).
 * A 32-bit atomic keeps live send changes off AMY's queue/render mutex.  The
 * render side converts the small number of active sends to SAMPLE once/block.
 */
static _Atomic uint32_t *s_levels = NULL;
static uint16_t s_bus_count = 0;
static void (*s_chained_hook)(uint16_t, SAMPLE *, uint16_t) = NULL;

/* Audio-thread-only state used when a route first activates a higher bus. */
static uint32_t s_seen_block = UINT32_MAX;
static uint16_t s_block_start_highest = 0;
static uint8_t *s_new_bus_cleared = NULL;

static inline size_t route_index(uint16_t source, uint16_t destination) {
    return (size_t)source * (size_t)s_bus_count + destination;
}

static inline float decode_level(uint32_t encoded) {
    return (float)encoded / 65535.0f;
}

static inline uint32_t encode_level(float level) {
    if (level <= 0.0f) return 0;
    if (level >= 1.0f) return 65535;
    return (uint32_t)(level * 65535.0f + 0.5f);
}

static void add_scaled_block(SAMPLE *destination, const SAMPLE *source,
                             uint16_t len, float level) {
    if (level <= 0.0f) return;
    const uint32_t count = (uint32_t)len * AMY_NCHANS;
    if (level >= 1.0f) {
        for (uint32_t i = 0; i < count; ++i) destination[i] += source[i];
        return;
    }
    const SAMPLE scale = F2S(level);
    for (uint32_t i = 0; i < count; ++i)
        destination[i] += MUL8_SS(scale, source[i]);
}

static void begin_block_if_needed(void) {
    const uint32_t block = amy_global.total_blocks;
    if (block == s_seen_block) return;
    s_seen_block = block;
    s_block_start_highest = amy_global.highest_bus;
    if (s_new_bus_cleared != NULL)
        memset(s_new_bus_cleared, 0, s_bus_count * sizeof(*s_new_bus_cleared));
}

static void ensure_destination_ready(uint16_t destination, uint16_t len) {
    if (destination >= s_bus_count) return;

    /*
     * amy_render() cleared only 0..highest_bus as it stood at block start.  A
     * send may be the first thing that ever activates a higher destination;
     * clear that allocated buffer once before accumulating into it.
     */
    if (destination > s_block_start_highest &&
        s_new_bus_cleared != NULL &&
        !s_new_bus_cleared[destination]) {
        memset(fbl[0][destination], 0,
               sizeof(SAMPLE) * (uint32_t)len * AMY_NCHANS);
        s_new_bus_cleared[destination] = 1;
    }

    if (destination > amy_global.highest_bus)
        amy_global.highest_bus = destination;
}

static void amy_bus_mixer_process(uint16_t source, SAMPLE *buf, uint16_t len) {
    /* Preserve host processing semantics: mixer sends the host-processed bus. */
    if (s_chained_hook != NULL) s_chained_hook(source, buf, len);

    if (s_levels == NULL || source >= s_bus_count || fbl[0] == NULL) return;
    begin_block_if_needed();

    /*
     * Route to other buses first.  A self-send is deliberately applied last so
     * its gain cannot alter the source copied to other destinations merely due
     * to destination-number iteration order.
     */
    for (uint16_t destination = 0; destination < s_bus_count; ++destination) {
        if (destination == source) continue;
        const uint32_t encoded = atomic_load_explicit(
            &s_levels[route_index(source, destination)], memory_order_relaxed);
        if (encoded == 0) continue;

        ensure_destination_ready(destination, len);
        add_scaled_block(fbl[0][destination], buf, len, decode_level(encoded));
    }

    const uint32_t self_encoded = atomic_load_explicit(
        &s_levels[route_index(source, source)], memory_order_relaxed);
    if (self_encoded != 0)
        add_scaled_block(buf, buf, len, decode_level(self_encoded));
}

int amy_bus_mixer_install(amy_config_t *config) {
    if (config == NULL) return -1;

    uint16_t buses = config->max_buses;
    if (buses == 0) buses = AMY_DEFAULT_NUM_BUSES;
    if (buses == 0) return -1;

    /* Installation is a startup/configuration operation, before amy_start(). */
    free(s_levels);
    free(s_new_bus_cleared);
    s_levels = NULL;
    s_new_bus_cleared = NULL;
    s_bus_count = 0;

    const size_t route_count = (size_t)buses * (size_t)buses;
    s_levels = malloc(route_count * sizeof(*s_levels));
    s_new_bus_cleared = calloc(buses, sizeof(*s_new_bus_cleared));
    if (s_levels == NULL || s_new_bus_cleared == NULL) {
        free(s_levels);
        free(s_new_bus_cleared);
        s_levels = NULL;
        s_new_bus_cleared = NULL;
        return -1;
    }
    for (size_t i = 0; i < route_count; ++i) atomic_init(&s_levels[i], 0);

    s_bus_count = buses;
    s_seen_block = UINT32_MAX;
    s_block_start_highest = 0;

    s_chained_hook = config->amy_external_bus_postprocess_hook;
    if (s_chained_hook == amy_bus_mixer_process) s_chained_hook = NULL;
    config->amy_external_bus_postprocess_hook = amy_bus_mixer_process;
    return 0;
}

void amy_bus_mixer_reset(void) {
    if (s_levels == NULL) return;
    const size_t route_count = (size_t)s_bus_count * (size_t)s_bus_count;
    for (size_t i = 0; i < route_count; ++i)
        atomic_store_explicit(&s_levels[i], 0, memory_order_relaxed);
}

int amy_bus_mixer_set(uint16_t source, uint16_t destination, float level) {
    if (s_levels == NULL || source >= s_bus_count || destination >= s_bus_count)
        return -1;
    atomic_store_explicit(&s_levels[route_index(source, destination)],
                          encode_level(level), memory_order_relaxed);
    return 0;
}

float amy_bus_mixer_get(uint16_t source, uint16_t destination) {
    if (s_levels == NULL || source >= s_bus_count || destination >= s_bus_count)
        return 0.0f;
    return decode_level(atomic_load_explicit(
        &s_levels[route_index(source, destination)], memory_order_relaxed));
}

uint16_t amy_bus_mixer_bus_count(void) {
    return s_bus_count;
}

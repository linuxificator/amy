// Regression coverage for the fork-only generic post-FX bus mixer.

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "amy.h"
#include "bus_mixer.h"

extern SAMPLE **fbl[AMY_MAX_CORES];

static int failures = 0;
static int chained_calls = 0;

#define CHECK(cond, fmt, ...) do {                                        \
    if (cond) printf("  ok   " fmt "\n", ##__VA_ARGS__);                  \
    else { printf("  FAIL " fmt "\n", ##__VA_ARGS__); failures++; }      \
} while (0)

static void chained_hook(uint16_t bus, SAMPLE *buf, uint16_t len) {
    (void)bus;
    chained_calls++;
    for (uint32_t i = 0; i < (uint32_t)len * AMY_NCHANS; ++i) buf[i] += 10;
}

static void test_control_surface(void) {
    printf("bus mixer control surface\n");
    CHECK(amy_bus_mixer_bus_count() == 3, "installed configured 3x3 matrix");
    CHECK(amy_bus_mixer_set(0, 2, 0.5f) == 0, "valid route accepted");
    CHECK(fabsf(amy_bus_mixer_get(0, 2) - 0.5f) < 0.00002f,
          "route level round-trips through atomic encoding");
    CHECK(amy_bus_mixer_set(0, 1, -2.0f) == 0
          && amy_bus_mixer_get(0, 1) == 0.0f,
          "negative level clamps to zero");
    CHECK(amy_bus_mixer_set(1, 2, 4.0f) == 0
          && amy_bus_mixer_get(1, 2) == 1.0f,
          "level above one clamps to unity");
    CHECK(amy_bus_mixer_set(3, 0, 1.0f) == -1
          && amy_bus_mixer_set(0, 3, 1.0f) == -1,
          "out-of-range source and destination are refused");
    amy_bus_mixer_reset();
    CHECK(amy_bus_mixer_get(0, 2) == 0.0f
          && amy_bus_mixer_get(1, 2) == 0.0f,
          "reset clears the complete matrix");
}

static void test_hook_chaining_and_routes(void) {
    printf("bus mixer render hook chaining and routing\n");
    const uint16_t len = 4;
    SAMPLE source[4 * AMY_NCHANS];
    for (uint32_t i = 0; i < (uint32_t)len * AMY_NCHANS; ++i) source[i] = 100;

    CHECK(amy_bus_mixer_set(0, 2, 0.5f) == 0, "forward route configured");
    amy_global.highest_bus = 0;
    amy_global.total_blocks++;
    memset(fbl[0][2], 0, sizeof(SAMPLE) * len * AMY_NCHANS);
    amy_global.config.amy_external_bus_postprocess_hook(0, source, len);
    SAMPLE expected = MUL8_SS(F2S(amy_bus_mixer_get(0, 2)), 110);
    CHECK(chained_calls == 1, "pre-existing host hook ran first");
    CHECK(source[0] == 110, "host hook modified the source before routing");
    CHECK(fbl[0][2][0] == expected,
          "destination received the host-processed source at route level");
    CHECK(amy_global.highest_bus == 2,
          "first send activates its higher destination bus");

    amy_bus_mixer_reset();
    CHECK(amy_bus_mixer_set(0, 0, 1.0f) == 0, "self-send configured");
    for (uint32_t i = 0; i < (uint32_t)len * AMY_NCHANS; ++i) source[i] = 50;
    amy_global.total_blocks++;
    amy_global.config.amy_external_bus_postprocess_hook(0, source, len);
    CHECK(source[0] == 120,
          "unity self-send doubles the source after the chained hook");
}

// examples.c calls this; the platform normally provides it.
void delay_ms(uint32_t ms) { (void)ms; }

int main(void) {
    amy_config_t config = amy_default_config();
    config.features.startup_bleep = 0;
    config.audio = AMY_AUDIO_IS_NONE;
    config.max_buses = 3;
    config.amy_external_bus_postprocess_hook = chained_hook;
    CHECK(amy_bus_mixer_install(&config) == 0, "install succeeds");
    amy_start(config);

    test_control_surface();
    test_hook_chaining_and_routes();

    amy_stop();
    if (failures) {
        printf("\n%d bus mixer check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nall bus mixer checks passed\n");
    return 0;
}

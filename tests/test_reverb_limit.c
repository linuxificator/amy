// Compile-time ceiling for memory-intensive built-in reverb networks.

#include <stdio.h>
#include "amy.h"

static int failures;
static uint8_t external_selector[1] = { 1 };

#define CHECK(c, message) do {                                            \
    if (c) printf("  ok   %s\n", message);                              \
    else { printf("  FAIL %s\n", message); ++failures; }                \
} while (0)

static void passthrough_return(uint16_t return_index, SAMPLE *block,
                               uint16_t frames, void *user_data) {
    (void)return_index;
    (void)block;
    (void)frames;
    (void)user_data;
}

static amy_config_t test_config(void) {
    amy_config_t config = amy_default_config();
    config.features.startup_bleep = 0;
    config.max_reverb_rooms = 1;
    return config;
}

static void test_builtin_ceiling(void) {
    puts("built-in shared return consumes the configured ceiling");
    amy_config_t config = test_config();
    amy_start(config);
    CHECK(amy_global.allocated_reverbs == 1,
          "one shared built-in reverb is allocated");
    config_reverb(0, 0.5f, 0.8f, 0.4f, 3000.0f);
    CHECK(amy_global.bus[0]->reverb.rev == NULL,
          "a legacy per-bus reverb cannot exceed the ceiling");
    CHECK(amy_global.bus[0]->reverb.level == 0,
          "a rejected per-bus reverb remains disabled");
    amy_stop();
}

static void test_external_return_does_not_count(void) {
    puts("external return leaves the built-in allowance available");
    amy_config_t config = test_config();
    config.aux_return_external = external_selector;
    config.amy_external_aux_return_process_hook = passthrough_return;
    amy_start(config);
    CHECK(amy_global.allocated_reverbs == 0,
          "external return consumes no built-in reverb slot");
    config_reverb(0, 0.5f, 0.8f, 0.4f, 3000.0f);
    CHECK(amy_global.bus[0]->reverb.rev != NULL,
          "legacy per-bus reverb can use the remaining slot");
    CHECK(amy_global.allocated_reverbs == 1,
          "per-bus allocation is counted");
    amy_stop();
}

void delay_ms(uint32_t ms) { (void)ms; }

int main(void) {
    CHECK(AMY_MAX_REVERBS == 1, "test uses an embedded-style ceiling of one");
    test_builtin_ceiling();
    test_external_return_does_not_count();
    if (failures) return 1;
    puts("all reverb ceiling checks passed");
    return 0;
}

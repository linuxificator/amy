// Tests the transport-neutral introspection query layer.
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "amy.h"
#include "introspection.h"

static int failures = 0;
static char response[16384];
static size_t response_len = 0;

#define CHECK(cond, fmt, ...) do {                                        \
    if (cond) { printf("  ok   " fmt "\n", ##__VA_ARGS__); }              \
    else { printf("  FAIL " fmt "\n", ##__VA_ARGS__); failures++; }       \
} while (0)

static void collect(const char *data, size_t len, void *context) {
    (void)context;
    if (response_len + len >= sizeof(response)) {
        failures++;
        return;
    }
    memcpy(response + response_len, data, len);
    response_len += len;
    response[response_len] = '\0';
}

static void clear_response(void) {
    response_len = 0;
    response[0] = '\0';
}

static void render_a_bit(void) {
    for (int i = 0; i < 16; ++i) amy_simple_fill_buffer();
}

static void send(const char *message) {
    amy_add_message((char *)message);
    render_a_bit();
}

static void query(const char *message) {
    clear_response();
    CHECK(amy_introspection_query(message, collect, NULL), "%s is handled", message);
}

static void test_namespace_and_version(void) {
    printf("query namespace and protocol version\n");
    clear_response();
    CHECK(!amy_introspection_query("v0w0Z", collect, NULL), "ordinary wire message is not consumed");
    query("?iv\n");
    CHECK(strcmp(response, "!iv1\n") == 0, "protocol version response is compact");
}

static void test_direct_karplus_state(void) {
    printf("directly configured Karplus-Strong state is introspectable\n");
    send("i20iv1in1Z");
    send("v0w6b0.985F1200R1.2i20Z");
    query("?is20\n");
    CHECK(strstr(response, "w6") != NULL, "wave is present");
    CHECK(strstr(response, "b0.985") != NULL, "feedback/decay is present");
    CHECK(strstr(response, "F1200") != NULL, "filter state is present");
    CHECK(strstr(response, "R1.2") != NULL, "resonance state is present");
    CHECK(strstr(response, "!ie\n") != NULL, "response has explicit end marker");
}

static void test_builtin_patch_state(void) {
    printf("built-in patch expands through the same live readback path\n");
    send("i21iv1K0Z");
    query("?is21");
    CHECK(strstr(response, "F179.93") != NULL, "patch 0 filter state is visible");
    CHECK(strstr(response, "R0.93") != NULL, "patch 0 resonance state is visible");
}

static void test_undefined_and_bad_queries(void) {
    printf("undefined synths and malformed queries remain bounded\n");
    query("?is60");
    CHECK(strcmp(response, "!ie\n") == 0, "undefined synth is an empty state stream");
    query("?is999");
    CHECK(strcmp(response, "!ix\n") == 0, "out-of-range synth is rejected");
    query("?iwut");
    CHECK(strcmp(response, "!ix\n") == 0, "unknown introspection query is rejected");
}

void delay_ms(uint32_t ms) { (void)ms; }

int main(void) {
    amy_config_t config = amy_default_config();
    config.features.startup_bleep = 0;
    amy_start(config);
    render_a_bit();

    test_namespace_and_version();
    test_direct_karplus_state();
    test_builtin_patch_state();
    test_undefined_and_bad_queries();

    if (failures) { printf("%d FAILURES\n", failures); return 1; }
    printf("all ok\n");
    return 0;
}

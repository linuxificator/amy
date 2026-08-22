// Compact AMY introspection built on the existing synth readback path.
#include "amy.h"
#include "introspection.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void introspection_write(amy_introspection_write_fn write_fn,
                                void *context,
                                const char *text) {
    if (write_fn == NULL || text == NULL) return;
    write_fn(text, strlen(text), context);
}

static bool only_trailing_space(const char *s) {
    while (*s != '\0') {
        if (!isspace((unsigned char)*s)) return false;
        ++s;
    }
    return true;
}

static void write_synth_state(uint8_t synth_num,
                              amy_introspection_write_fn write_fn,
                              void *context) {
    char command[MAX_MESSAGE_LEN];
    char prefix[24];
    void *state = NULL;

    snprintf(prefix, sizeof(prefix), "!is%u:", (unsigned)synth_num);

    do {
        command[0] = '\0';
        state = yield_synth_commands(synth_num, command, sizeof(command), true, state);
        if (command[0] == '\0') continue;
        introspection_write(write_fn, context, prefix);
        introspection_write(write_fn, context, command);
        introspection_write(write_fn, context, "\n");
    } while (state != NULL);

    introspection_write(write_fn, context, "!ie\n");
}

bool amy_introspection_query(const char *message,
                             amy_introspection_write_fn write_fn,
                             void *context) {
    if (message == NULL || strncmp(message, "?i", 2) != 0) return false;

    const char *query = message + 2;
    if (query[0] == 'v' && only_trailing_space(query + 1)) {
        char response[20];
        snprintf(response, sizeof(response), "!iv%d\n", AMY_INTROSPECTION_PROTOCOL_VERSION);
        introspection_write(write_fn, context, response);
        return true;
    }

    if (query[0] == 's') {
        char *end = NULL;
        unsigned long synth_num = strtoul(query + 1, &end, 10);
        if (end != query + 1 && synth_num <= UINT8_MAX && only_trailing_space(end)) {
            write_synth_state((uint8_t)synth_num, write_fn, context);
            return true;
        }
    }

    introspection_write(write_fn, context, "!ix\n");
    return true;
}

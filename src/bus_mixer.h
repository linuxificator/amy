#ifndef AMY_BUS_MIXER_H
#define AMY_BUS_MIXER_H

#include <stdint.h>
#include "amy.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Generic post-FX bus send mixer.
 *
 * Install it on an amy_config_t before amy_start(), then configure any number
 * of source -> destination sends with levels in [0, 1].  Routes are deliberately
 * unrestricted: forward, backward, self and cyclic sends are all accepted.
 *
 * The mixer runs at the end of each source bus' normal AMY FX chain.  Because
 * AMY processes buses in ascending numeric order, routing order is observable:
 *
 *   source < destination  destination receives the send before its FX run
 *   source > destination  destination already ran its FX this block, so the
 *                         send is added after those FX for this block
 *   source == destination the post-FX bus is mixed back into itself once
 *
 * Cycles do not recurse.  They are evaluated once in normal bus order per
 * audio block.  This makes every graph deterministic without forbidding useful
 * feedback-style routing, while making clear that cyclic graphs are inherently
 * order-dependent.
 *
 * The mixer chains an existing native amy_external_bus_postprocess_hook: that
 * hook runs first, and its resulting source buffer is what gets sent.
 */
int amy_bus_mixer_install(amy_config_t *config);

/* Remove every send but keep the installed mixer ready for reuse. */
void amy_bus_mixer_reset(void);

/*
 * Set/get one source -> destination send.  set() clamps level to [0, 1].
 * Returns 0 on success and -1 if either bus is outside the installed range.
 * The level table uses atomic 32-bit updates, so control code may change sends
 * while the audio thread is rendering without taking AMY's render/queue lock.
 */
int amy_bus_mixer_set(uint16_t source, uint16_t destination, float level);
float amy_bus_mixer_get(uint16_t source, uint16_t destination);

/* Number of buses in the installed matrix, or 0 when not installed. */
uint16_t amy_bus_mixer_bus_count(void);

#ifdef __cplusplus
}
#endif

#endif

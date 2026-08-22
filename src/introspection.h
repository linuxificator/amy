// Compact, transport-neutral AMY introspection.
//
// This deliberately exposes AMY's existing wire representation instead of
// adding UI labels, ranges, units, or per-patch metadata to embedded builds.
#ifndef __AMY_INTROSPECTION_H
#define __AMY_INTROSPECTION_H

#include <stdbool.h>
#include <stddef.h>

#define AMY_INTROSPECTION_PROTOCOL_VERSION 1

typedef void (*amy_introspection_write_fn)(const char *data, size_t len, void *context);

// Handle an introspection query. Returns true when `message` belongs to the
// introspection namespace, false when the caller should pass it elsewhere.
//
// Queries:
//   ?iv       -> !iv1\n
//   ?is<N>    -> zero or more !is<N>:<AMY wire command>\n lines,
//                followed by !ie\n
//
// The synth-state response is generated from AMY's live synth readback, so it
// works for built-in patches, memory patches, and directly configured synths.
// It intentionally returns the complete technical state. Presentation/filter
// policy belongs in the client, not in embedded AMY.
bool amy_introspection_query(const char *message,
                             amy_introspection_write_fn write_fn,
                             void *context);

#endif

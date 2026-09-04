# LB Omnichord AMY release contract

LB Omnichord consumes AMY from a fork release branch named
`releases/amy_omnichord_R<YYYYMMDD>T<HHMMSS>`. The consumer records both the
branch and exact commit SHA. The branch explains provenance; the SHA is the
immutable build input used by every platform package.

The fork's `main` remains a fast-forward mirror of `shorepine/amy` `main`.
Generic changes are developed on a clean upstream-directed branch. A release
branch layers the tested platform and application profile on that clean work;
it is never itself offered upstream.

## Current line

`releases/amy_omnichord_R20260903T202802` starts with:

- Shorepine main `0fb0a00b5a9f9443d7e1f85261cc7e70a0adb76b`;
- the generic sequencer-group work from `rework/sequencer`;
- the private Unix-socket service and Android Oboe integration;
- the Gamma9001 hosted drum bank profile;
- deterministic offline CPython startup for tests; and
- the larger bounded sequencer-group capacity required by the rhythm
  catalogue.

The Unix-socket receiver applies lossless backpressure when its bounded
realtime handoff queue is full. Large startup transactions therefore remain in
the kernel socket queue instead of being read and discarded.

The abandoned bus-mixer experiment is not part of this line. AMY's generic
bus support remains whatever is present in Shorepine main; no private mixer
module or routing policy is restored.

## Sequencer boundary

The clean `rework/sequencer` branch contains only generic AMY behavior:

- grouped events use `ticks=tick,period,event_tag,group_tag`;
- one `sequence_control` family publishes, starts, stops, gates and clears;
- active executions retain immutable published revisions;
- one, N and infinite repeats share the same repeat-count model;
- quantization uses AMY's own sequencer clock; and
- a root event may launch a group, while a group cannot launch another group.

LB Omnichord owns all musical policy: which rhythm roles become groups, which
ones a fill gates, which arpeggios may overlap, group/tag allocation and root
arrangement schedules. The frontend remains a wire-protocol client and never
imports or calls AMY engine internals.

The release profile uses 1,024 group slots, 64 local event tags per group and
40 active or pending executions. The high group count stores the complete fill
catalogue; it does not create 1,024 players. The execution pool includes room
for the characterized worst case of 34 concurrent role, fill and overlapping
arpeggio executions. Event tables are allocated lazily only for definitions
that are actually authored.

## Platform boundary

On Android, the Qt frontend and the unexported `:amy` service are separate
processes under the same application UID. The frontend discovers the
application-private socket path and sends only AMY wire messages. Audio is
rendered by the service and handed to Oboe/AAudio. The service is built at 48
kHz with 128-frame stereo blocks.

Desktop Linux and macOS use the same frontend wire protocol over a private
Unix socket. Windows may use its wrapper/named-pipe transport, but the AMY
message stream and frontend logic stay platform-independent.

The Android AAR defines `GAMMA9001` and generates its linkable sample blob in
a private per-ABI build directory. Native downstream builds use the same
`gamma9001-blob-c` generator and link its output while defining `GAMMA9001`.
Consequently PCM presets 0-18 consistently mean the Gamma808 ROM bank on these
targets; this profile changes no wire or sequencer semantics.

The CPython `AMY_PCM_BANK` build selector is release/build policy rather than
generic sequencer behavior. `AMY_PCM_BANK=tiny` omits Gamma9001; the default
for this release line is Gamma9001. Both choices force a fresh extension build
because they share an output filename.

`amy.live(audio=AMY_AUDIO_IS_NONE, ...)` is the deterministic test mode. The
default remains live miniaudio, preserving existing callers. Offline mode
prevents a system-audio callback and a deterministic renderer from consuming
the same AMY stream concurrently.

## Release procedure

1. Verify fork main exactly matches the chosen Shorepine main.
2. Test generic work on the clean upstream-directed branch.
3. Create the release branch and add only required fork integrations.
4. Run native AMY, wire/socket, PCM-bank, offline and Android contract tests.
5. Pin the final release branch and SHA in LB Omnichord configuration and
   packaging inputs.
6. Run LB Omnichord's generic and platform-specific suites against that same
   SHA.
7. Record the exact AMY SHA in release notes and keep diagnostic commits.

ESP32 validation is deliberately deferred for this rework; it must be
completed before claiming ESP32 support for the resulting release.

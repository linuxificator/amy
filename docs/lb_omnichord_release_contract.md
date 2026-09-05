# LB Omnichord AMY release contract

LB Omnichord consumes AMY from a fork release branch named
`releases/amy_omnichord_R<YYYYMMDD>T<HHMMSS>`. The consumer records both the
branch and exact commit SHA. The branch explains provenance; the SHA is the
immutable build input used by every platform package.

The fork's `main` remains a fast-forward mirror of `shorepine/amy` `main`.
Generic changes are developed on a clean upstream-directed branch. A release
branch starts from that clean work and layers only the tested platform and
application profile on top; it is never itself offered upstream.

## Current line

`releases/amy_omnichord_R20260905T104903` starts from fork branch
`rework/sequencer` at `3872b4be16af4f486c8f3259d44478ee7174864f`, the
source offered in Shorepine PR 1151. That source in turn starts from Shorepine
main `0fb0a00b5a9f9443d7e1f85261cc7e70a0adb76b`.

The release layers on:

- the private Unix-socket service and Android Oboe integration;
- socket receiver backpressure protection;
- the Gamma9001 hosted drum-bank profile;
- deterministic offline CPython startup for tests;
- 336 oscillators and 11 buses; and
- 1,280 sequence tags, 64 events per definition and 40 active or
  alignment-pending executions.

The abandoned bus-mixer experiment is not part of this line. The 11-bus
setting only enlarges AMY's existing generic bus capacity; it introduces no
private mixer, routing API or musical policy.

## Sequence boundary

The clean `rework/sequencer` branch contains only generic AMY behavior:

- untagged one- and two-field `ticks` retain direct scheduling;
- a tagged `ticks=(tick, period, tag)` event cumulatively extends a stopped,
  reusable sequence definition;
- `sequence_reset` clears a future definition;
- `sequence_control` starts, stops or gates executions, with optional
  alignment on AMY's own clock;
- finite executions may overlap and each execution retains its immutable
  definition snapshot;
- publication and deferred reclamation keep clone/free work out of the render
  path; and
- a sequence may start another sequence, while bounded execution capacity
  prevents cyclic graphs from recursing without limit.

LB Omnichord owns all musical policy: instrument roles, fills, arpeggios,
sequence/tag allocation and replacement boundaries. The frontend remains a
wire-protocol client and never imports or calls AMY engine internals.

The high tag capacity stores the complete rhythm catalogue. It does not create
1,280 players: definitions and executions allocate from separate bounded
resources, and only authored definitions consume event storage.

## Platform boundary

On Android, the Qt frontend and the unexported `:amy` service are separate
processes under the same application UID. The frontend discovers the
application-private socket path and sends only AMY wire messages. Audio is
rendered by the service and handed to Oboe/AAudio. The service is built at
48 kHz with 128-frame stereo blocks.

Desktop Linux and macOS use the same frontend wire protocol over a private
Unix socket. Windows uses its wrapper/named-pipe transport. The AMY command
stream and frontend synthesis logic stay platform-independent.

The Android AAR defines `GAMMA9001` and generates its linkable sample blob in
a private per-ABI build directory. Native downstream builds use the same
`gamma9001-blob-c` generator and link its output while defining `GAMMA9001`.
Consequently PCM presets 0-18 mean the Gamma808 ROM and presets 256-391 use
the Gamma9001 sample set on all hosted release targets.

The CPython `AMY_PCM_BANK` selector is release/build policy rather than
generic sequencer behavior. `AMY_PCM_BANK=tiny` omits Gamma9001; the hosted
Omnichord profile selects Gamma9001. Both choices force a fresh extension
build because they share an output filename.

`amy.live(audio=False, ...)` is the deterministic host-test mode. The default
remains live miniaudio, preserving existing callers. Offline mode prevents a
system-audio callback and a deterministic renderer from consuming the same
AMY stream concurrently.

The release also keeps compile-time embedded audio geometry configurable,
including the already characterized 48 kHz / 128-sample ESP32-P4 frame size.
Physical ESP32-P4 timing, heap and DMA validation remains a separate hardware
gate and is not implied by hosted tests.

## Release procedure

1. Verify fork main exactly matches the chosen Shorepine main.
2. Test generic work on the clean upstream-directed branch.
3. Start a new immutable release branch at that exact generic commit.
4. Add only required fork integrations in diagnostic commits.
5. Run native AMY, wire/socket, PCM-bank, offline and Android contract tests.
6. Pin the final release branch and SHA once in LB Omnichord's release-input
   manifest and update its human-readable platform documents.
7. Reinstall that exact AMY SHA and run LB Omnichord's generic and
   platform-specific suites.
8. Record the exact AMY SHA in release notes.

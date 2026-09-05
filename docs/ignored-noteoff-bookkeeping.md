# Ignored note-offs and voice-stealing bookkeeping

## Summary

`SYNTH_FLAGS_IGNORE_NOTE_OFFS` is intended for sounds such as one-shot drums:
their natural decay should continue and the caller does not need to send a
matching note-off. A polyphonic synth using that flag can still steal voices
when more notes arrive than it has voices.

Before this correction, each stolen or retriggered note was nevertheless
stored in the synth's bounded forgotten-note pool. That pool exists only so a
later note-off for a stolen note can be recognized and absorbed. Because a
caller using `SYNTH_FLAGS_IGNORE_NOTE_OFFS` may never send those note-offs, the
entries were never removed. After 16 distinct stolen notes, AMY repeatedly
reported `forgotten pool overflow`.

## Existing behavior in main

This issue predates reusable sequences. It was reproduced on the unmodified
`shorepine/main` commit `0fb0a00b` using only an ordinary four-voice synth with
`SYNTH_FLAGS_IGNORE_NOTE_OFFS` and a stream of direct note-on events. No
sequence definition, execution, or control operation was involved.

Reusable sequences can make the issue easier to encounter because they are a
convenient way to run a long stream of one-shot events, but they neither cause
the bookkeeping mismatch nor participate in its correction.

## Correction

For a synth with `SYNTH_FLAGS_IGNORE_NOTE_OFFS`, AMY now:

- does not store a stolen or retriggered note in the forgotten-note pool;
- silently accepts a late unmatched note-off, consistent with the flag; and
- clears obsolete forgotten-note entries when an existing synth transitions
  from ordinary note-off handling to ignored note-offs.

The render and voice-stealing behavior is unchanged. The correction removes
bookkeeping whose only consumer—the later note-off—has explicitly been
disabled. It introduces no API or wire-protocol field.

Synths without `SYNTH_FLAGS_IGNORE_NOTE_OFFS` retain the existing behavior:
stolen notes are remembered so their later note-offs can be matched without a
warning.

## Regression coverage

`tests/test_ignore_note_offs.c` verifies both sides of the behavior:

1. sixty-four distinct one-shot note-ons sent to a four-voice ignored-note-off
   synth leave the forgotten-note pool empty; and
2. five note-ons sent to an ordinary four-voice synth leave exactly one stolen
   note in the pool.

The pool inspection hook is compiled only for this test. Production builds do
not expose or include the test accessor.


# LB Omnichord release contract

LB Omnichord consumes AMY only from a fork branch named
`releases/amy_omnichord_R<YYYYMMDD>T<HHMMSS>`. A consumer must pin both that
branch name and its exact commit SHA. The branch is useful context for people;
the SHA is the immutable, reproducible build input and must be recorded in the
LB Omnichord GitHub release notes.

The fork's `main` branch is a fast-forward mirror of `shorepine/amy` `main`.
Feature work is never merged into it. Before creating a new Omnichord release,
fetch shorepine, fast-forward the fork's `main` when necessary, and incorporate
those upstream changes into the new release branch.

The first Omnichord release branch combines the tested LB integrations. Every
later Omnichord release branch starts from the preceding release branch, then
adds the verified upstream and integration changes for that release. Release
branches also contain the internal `work/codex_info` handoff material; upstream
offer branches must remain free of that internal material.

LB Omnichord CI must verify that a clone checked out by SHA resolves to the
declared SHA and that the declared release branch contains it. A release must
not be published unless its platform packages and regression gates all use the
declared AMY release input.

## This release

This initial release combines:

- shorepine `main` at `81cddfa8610c570a3a255a17ef5dfd81892849bb`;
- the private Unix socket API and Android AMY/Oboe service from
  `integration/amy_android`;
- the LB Omnichord bus-mixer implementation used by native desktop tests and
  packages; and
- the internal `work/codex_info` handoff.

The Android frontend remains a wire-protocol client. Platform integration is a
startup preamble only: it discovers the app-private socket path and connects to
the separately running AMY service. AMY audio output is Oboe/AAudio, not
PulseAudio.

## Nested-sequencer integration workflow

The nested-sequencer work follows two deliberately separate histories:

1. `upstream/nested_sequencer` starts directly at Shorepine `main`
   `81cddfa8610c570a3a255a17ef5dfd81892849bb`. It contains only the reusable
   AMY engine/API, tests, and public documentation. It does not contain release
   integrations or internal handoff material.
2. `releases/amy_omnichord_R20260830T191146` starts at the exact tip of the
   preceding release, `8c74a1681fa6a3b430ddee9390294bccb8f55a86`, so it keeps
   the already-tested socket, Android/Oboe, bus-mixer, and release-contract
   changes. The two nested-sequencer commits are then cherry-picked from the
   upstream branch. The only conflict was the C-test list: both
   `test_bus_mixer` and `test_nested_sequencer` are retained.
3. LB Omnichord must pin the resulting release-branch SHA, author its rhythms
   as stored patterns, use loop mode for the base rhythm and one-shot mode for
   fills, and pass all existing platform and release tests.
4. Only after the complete Omnichord behavior is verified may the clean
   `upstream/nested_sequencer` branch be proposed to Shorepine. The release
   branch itself is never the source of that pull request.

This ordering makes LB Omnichord the integration proof without leaking its
platform-specific code into the reusable upstream proposal. If integration
finds a generic AMY defect, fix and test it first on `upstream/nested_sequencer`,
then cherry-pick that additional commit into the current release branch and
update LB Omnichord's exact SHA pin.

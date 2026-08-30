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

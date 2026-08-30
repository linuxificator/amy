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

This release line also provides the integration-only `AMY_PCM_BANK` build
selector used by LB packaging. `AMY_PCM_BANK=tiny` omits Gamma9001; both bank
choices force a fresh extension rebuild because their output filename is the
same. The default remains AMY's Gamma9001 CPython build.
This selector is wrapper/build policy and is deliberately absent from the
clean `upstream/nested_sequencer` proposal.

## This release

The initial experimental release combined:

- shorepine `main` at `81cddfa8610c570a3a255a17ef5dfd81892849bb`;
- the private Unix socket API and Android AMY/Oboe service from
  `integration/amy_android`;
- an experimental LB Omnichord bus-mixer implementation; and
- the internal `work/codex_info` handoff.

The Android frontend remains a wire-protocol client. Platform integration is a
startup preamble only: it discovers the app-private socket path and connects to
the separately running AMY service. AMY audio output is Oboe/AAudio, not
PulseAudio.

The bus-mixer experiment was subsequently abandoned. It is deliberately absent
from the current release line and from all upstream proposals. Omnichord rhythm
fills must use the nested sequencer's event gating and must not depend on a
private audio-routing extension.

## Nested-sequencer integration workflow

The nested-sequencer work follows two deliberately separate histories:

1. `upstream/nested_sequencer` starts directly at Shorepine `main`
   `81cddfa8610c570a3a255a17ef5dfd81892849bb`. It contains only the reusable
   AMY engine/API, tests, and public documentation. It does not contain release
   integrations or internal handoff material.
2. `releases/amy_omnichord_R20260830T191146` starts at the exact tip of the
   preceding release, `8c74a1681fa6a3b430ddee9390294bccb8f55a86`, so it keeps
   the already-tested socket, Android/Oboe, and release-contract changes. The
   abandoned bus-mixer merge is explicitly reversed, and the two
   nested-sequencer commits are cherry-picked from the upstream branch.
3. LB Omnichord must pin the resulting release-branch SHA, author its rhythms
   as stored patterns, use loop mode for the base rhythm and one-shot mode for
   fills, and pass all existing platform and release tests. Each logical
   percussion role is a tagged loop instance. A fill stores generic `zQM`
   events for the role instances it suppresses; deciding which musical roles
   continue is exclusively LB Omnichord policy and is not AMY engine code.
4. Only after the complete Omnichord behavior is verified may the clean
   `upstream/nested_sequencer` branch be proposed to Shorepine. The release
branch itself is never the source of that pull request.

Live fill schedules use AMY's root `zQA` trigger scheduler. Replacing or
clearing those ordinary root tags changes only future fill launches: a
one-shot which is already active retains its immutable definition and finishes.
This keeps the frontend wire-only and avoids a host timer which tries to follow
AMY's musical clock.

This ordering makes LB Omnichord the integration proof without leaking its
platform-specific code into the reusable upstream proposal. If integration
finds a generic AMY defect, fix and test it first on `upstream/nested_sequencer`,
then cherry-pick that additional commit into the current release branch and
update LB Omnichord's exact SHA pin.

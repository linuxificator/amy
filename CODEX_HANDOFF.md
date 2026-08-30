# AMY socket API extra: work handoff

Last updated: 2026-08-30 (Europe/Amsterdam)

This file belongs only on the fork's `work/codex_info` branch. Do not merge or
cherry-pick it into `upstream/amy_socket_api_xtra` or an upstream pull request.

## Outcome and current state

The earlier full Android offers were replaced with a deliberately small,
portable upstream offer based on current `shorepine/amy:main`.

- Upstream offer branch: `linuxificator/amy:upstream/amy_socket_api_xtra`
- Offer head: `d90917c2aa165a01a2dbfd9ef6482692d4d5b546`
- Upstream base used: `81cddfa8610c570a3a255a17ef5dfd81892849bb`
- Open replacement PR: <https://github.com/shorepine/amy/pull/1147>
- Closed superseded PR: <https://github.com/shorepine/amy/pull/1136>
- Closed superseded PR: <https://github.com/shorepine/amy/pull/1138>

The old branches were intentionally retained:

- `origin/upstream/android-oboe` at
  `a12c19bfdd9936dcb10ab8b8e39214041dd56cd9`
- `origin/upstream/godot-android` at
  `f77cf9389a800a69396170f502f8c6b380ca2329`

PR #1136 and #1138 were closed with comments pointing to #1147. No branch was
deleted. The bus-mixer work was not mixed into this offer. A local stash was
left untouched when this branch was prepared:

```text
stash@{0}: On feature/bus-mixer: preserve setup.py PCM-bank work before upstream/amy_socket_api_xtra
```

Always inspect the current stash list before referring to that numeric stash
index, because later stashes can renumber it.

## Why the replacement exists

In the review of PR #1136, Brian Whitman explained that shorepine cannot own a
full Android/Oboe/Qt/Godot integration with its framework-specific dependency
and testing burden. He invited a replacement containing only:

1. the portable Linux/Android Unix packet transport and a plain Linux test;
2. platform-independent Godot `backend_ready` / `backend_error` signals;
3. short Android porting notes, including the NDK flags, allocator declarations,
   and the AMY master-volume scale;
4. prominent links to the full implementations maintained in this fork.

PR #1147 follows that boundary. It intentionally excludes Gradle projects,
Oboe service code, Android examples, the Godot Android project, and emulator
workflows.

## Offer branch commits

The offer is four reviewable commits on top of the upstream base:

```text
d90917c2 Document portable service integrations
237ff60b Expose Godot backend lifecycle signals
4104247a Harden Unix socket transport and tests
1bdc0d8e Add private Unix socket transport for AMY wire protocol
```

The final upstream diff contains 11 files and no Codex, ChatGPT, OpenAI, or
generated-by attribution:

- `.github/workflows/unix-socket.yml`
- `.github/workflows/c-cpp.yml`
- `README.md`
- `docs/godot.md`
- `docs/porting.md`
- `godot/amy.gd`
- `src/amy_unix_socket.c`
- `src/amy_unix_socket.h`
- `tests/run_amy_unix_socket_test.sh`
- `tests/test_amy_unix_socket.c`
- `tests/test_godot_backend_signals.py`

## Unix transport contract

`src/amy_unix_socket.[ch]` is a transport helper, not an AMY audio backend.
It supports Linux and Android pathname `AF_UNIX` / `SOCK_SEQPACKET`; other
platforms get `-ENOTSUP` stubs.

Important invariants:

- The receiver thread never calls AMY and never runs in the audio callback.
- One producer copies complete packets into a fixed 64-entry SPSC queue.
- The AMY/control owner drains that queue at a safe boundary and calls
  `amy_add_message()` itself.
- Maximum request/reply size is `MAX_MESSAGE_LEN - 1`, preserving space for the
  NUL added by `amy_unix_socket_receive()`.
- One client is active at a time. Accepted Linux/Android peers must have the
  server's effective UID (`SO_PEERCRED`).
- The pathname is mode `0600`.
- Startup preserves non-socket paths and live listeners; it removes only a
  same-UID socket that refuses a connection.
- Shutdown unlinks only the exact device/inode created by that server, so a
  replacement file or socket is not deleted.
- Receive and send calls are non-blocking. Send is for a control/status path,
  not the realtime audio callback.
- Queue overruns, oversized packets, and rejected peers have monotonic counters.

Public calls are `amy_unix_socket_start`, `amy_unix_socket_stop`,
`amy_unix_socket_receive`, `amy_unix_socket_send`, and the three diagnostic
counter accessors.

The sanitizer regression covers invalid inputs, permissions, request/reply at
the packet limit, oversize dropping, non-consuming `EMSGSIZE`, exact queue
ordering and overrun counts, second-client rejection, reconnects, active and
stale socket paths, regular-file preservation, and shutdown path replacement.

## Godot contract

Only the shared `godot/amy.gd` wrapper changed:

- `backend_ready` is emitted after `_started = true` for either native or web.
- `backend_error(message: String)` is emitted for a missing native GDExtension
  or a ten-second web startup timeout.
- Consumers must connect the signals before `add_child(amy)`, because the native
  backend can initialize synchronously in `_ready()`.

`tests/test_godot_backend_signals.py` guards the public signatures and success /
failure wiring. The existing `make godot-api` generator preserves these edits,
and `gdparse` accepts the result.

## Android reference and porting facts

The complete reference stays at:

- <https://github.com/linuxificator/amy/tree/upstream/android-oboe>
- <https://github.com/linuxificator/amy/tree/upstream/godot-android>

Relevant files in `upstream/android-oboe` include:

- `android/amy-service/src/main/cpp/CMakeLists.txt`
- `android/amy-service/src/main/cpp/amy_android.cpp`
- `android/amy-service/src/main/cpp/amy_android_capture.cpp`
- `android/amy-service/src/main/cpp/amy_android_daisy_alloc.h`
- `android/amy-service/src/main/java/org/amy/audio/AmyService.java`
- `docs/android_unix_socket.md`
- `.github/workflows/android.yml`

Verified build definitions are:

```text
AMY_DAISY=1
AMY_HOST_MIDI=1
AMY_NO_MINIAUDIO=1
AMY_WAVETABLE=1
```

`AMY_DAISY` selects the existing 48 kHz / 128-frame profile.
`AMY_NO_MINIAUDIO` leaves audio ownership to Oboe. `AMY_HOST_MIDI` leaves MIDI
lifecycle hooks to the host. The build force-includes only these declarations
for C translation units so `pcm.c` can see the allocators already provided by
`delay.c` under `AMY_DAISY`:

```c
#include <stddef.h>
void *qspi_malloc(size_t size);
void qspi_free(void *ptr);
```

Do not link a second allocator implementation.

AMY's `V` bus/master value is `0..10`; final mixdown applies `V * 0.1`.
Therefore `V2.0` is 20 percent linear gain and `V10.0` is full master gain.
This is distinct from oscillator amplitude/velocity and per-synth `iV`.

## Windows named-pipe finding

No AMY core change is required for the LB_Omnichord Windows named pipe. Do not
copy the service into AMY unless an upstream maintainer explicitly expands the
scope. It is a normal native host around AMY's public embedding calls.

The source of truth inspected was LB_Omnichord `origin/main` at
`387776cffad7394c1fcf6add1ced5d3e69a8d382`. The named-pipe transport originally
landed in `906b4c5308075a8613b46d008c6d50f2113e55d0`.

Exact immutable references:

- [service](https://github.com/linuxificator/LB_Omnichord/blob/387776cffad7394c1fcf6add1ced5d3e69a8d382/amysynth_version/qt_frontend/packaging/windows/amy_service.c)
- [launcher](https://github.com/linuxificator/LB_Omnichord/blob/387776cffad7394c1fcf6add1ced5d3e69a8d382/amysynth_version/qt_frontend/packaging/windows/run_windows.ps1)
- [Qt client](https://github.com/linuxificator/LB_Omnichord/blob/387776cffad7394c1fcf6add1ced5d3e69a8d382/amysynth_version/qt_frontend/code/amy_transport.py)
- [CMake target](https://github.com/linuxificator/LB_Omnichord/blob/387776cffad7394c1fcf6add1ced5d3e69a8d382/amysynth_version/qt_frontend/packaging/windows/CMakeLists.txt)
- [packaging tests](https://github.com/linuxificator/LB_Omnichord/blob/387776cffad7394c1fcf6add1ced5d3e69a8d382/amysynth_version/qt_frontend/tests/test_packaging.py)
- [release workflow](https://github.com/linuxificator/LB_Omnichord/blob/387776cffad7394c1fcf6add1ced5d3e69a8d382/.github/workflows/desktop-release.yml)
- [Windows design notes](https://github.com/linuxificator/LB_Omnichord/blob/387776cffad7394c1fcf6add1ced5d3e69a8d382/amysynth_version/qt_frontend/docs/WINDOWS_NATIVE.md)

The implementation uses one `CreateNamedPipeA` byte-mode instance with
`PIPE_ACCESS_DUPLEX` and `PIPE_REJECT_REMOTE_CLIENTS`. The launcher chooses a
unique GUID-based name and a readiness file. Qt uses `QLocalSocket`. Records
are LF-framed because the pipe is a byte stream; the service buffers partial
and multiple `ReadFile` results and accepts only completed AMY requests ending
in `Z`. It then calls `amy_add_message()`.

The service initializes with `amy_default_config()` / `amy_start()`, renders
with `amy_simple_fill_buffer()`, and shuts down with `amy_stop()`. Hosted Windows
CI proves native compilation, non-silent offline rendering, Qt-to-pipe-to-AMY
delivery, packaging, and cleanup. It does not prove physical audio or MIDI,
latency, or dropout behavior.

## Validation record

All authoritative local checks passed at offer head `d90917c2`:

```text
bash tests/run_amy_unix_socket_test.sh
  PASS (ASan, UBSan, leak detection)

make ctest
  PASS (all native C tests)

AMY_TEST_THRESHOLD_DB=-70.0 make test PYTHON=/tmp/amy-test-venv/bin/python
  PASS (133 tests; exact threshold used by upstream CI)

python3 tests/test_godot_backend_signals.py
  PASS (3 tests)

gdparse godot/amy.gd
  PASS (gdtoolkit 4.5.0)

make godot-api
make check-c-api
git diff --check upstream/main...HEAD
  PASS
```

An earlier `make test` without `AMY_TEST_THRESHOLD_DB=-70.0` used the default
bit-exact threshold and reported normal floating-point drift. This was not the
upstream CI configuration. The rerun with the workflow's exact threshold
passed all 133 tests.

## GitHub Actions and next actions

Immediately after PR creation, GitHub registered four workflows but marked
them `action_required`, which is the fork-workflow approval gate rather than a
test result:

- [Arduino CI](https://github.com/shorepine/amy/actions/runs/33304036269)
- [C/C++ CI](https://github.com/shorepine/amy/actions/runs/33304036278)
- [AMY HW CI build](https://github.com/shorepine/amy/actions/runs/33304036291)
- [Unix socket transport](https://github.com/shorepine/amy/actions/runs/33304036310)

Next maintainer-facing work:

1. wait for shorepine to approve and run the fork workflows;
2. inspect all PR #1147 checks and any review comments;
3. make review-driven edits only on `upstream/amy_socket_api_xtra`, preserving
   the narrow scope;
4. update this handoff separately on `work/codex_info` if state changes;
5. never delete the retained `upstream/android-oboe` or
   `upstream/godot-android` branches without explicit user instruction.

## Repository and authentication notes

Remotes:

```text
origin   git@github.com:linuxificator/amy.git
upstream https://github.com/shorepine/amy.git
```

This repository has a local `core.sshCommand` using
`/home/jeroen/.ssh/amy_github` with `IdentitiesOnly=yes`. That deploy key has
write access and successfully pushed the offer branch. GitHub CLI authentication
comes from the desktop keyring and works outside the restricted sandbox; the
plaintext `~/.config/gh/hosts.yml` token can appear stale from inside a sandbox.
Do not replace working authentication merely because a sandboxed `gh auth`
probe sees that stale token.

## Safe branch discipline

- `upstream/amy_socket_api_xtra` must remain free of this handoff and other
  Codex-specific files.
- `work/codex_info` may contain operational history and agent-oriented notes,
  but must never be offered upstream.
- Keep framework-specific Android work on the two retained reference branches.
- Rebase or merge future upstream changes only after inspecting whether they
  affect the socket API, `godot/amy.gd`, generated bindings, or porting facts.
- Preserve unrelated dirty-worktree changes and stashes.

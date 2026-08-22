# AMY Android service

This directory builds AMY as an Android AAR whose native engine runs in an
unexported `:amy` service process. The service owns Oboe/AAudio output and
accepts native AMY wire messages over a private pathname Unix socket.

The design deliberately keeps musical control out of JNI:

```text
Qt/Python process
    |
    | AF_UNIX/SOCK_STREAM
    | <app filesDir>/amy.sock
    | newline-delimited AMY wire messages
    v
Android :amy service process
    |
    +-- blocking Unix socket receive thread
    +-- fixed SPSC command ring
    +-- AMY C engine
    +-- Oboe low-latency output callback
            |
            v
          AAudio
```

The service is declared `android:exported="false"` and runs as `:amy`, so it
has the same application UID as the Qt host but a separate process. The Java
wrapper only accepts the exact filename `amy.sock` directly below the app's
private `filesDir`. The native server additionally creates the socket node as
mode `0600`.

## Audio profile

The Android build uses AMY's existing 48 kHz / 128-frame profile and does not
link AMY's miniaudio backend. Oboe requests:

- stereo signed 16-bit output
- 48 kHz
- `PerformanceMode::LowLatency`
- `SharingMode::Exclusive`
- callback-driven output

Oboe callback sizes are not assumed to equal the AMY block size. The callback
keeps only the unconsumed tail of the current AMY block and renders a new
128-frame block exactly when needed. There is no extra 128-frame output ring.

All ordinary AMY access occurs on the Oboe callback thread. The socket thread
only copies complete wire messages into a preallocated SPSC ring. At each AMY
block boundary the callback consumes at most 64 queued commands, then calls
`amy_simple_fill_buffer()`.

## Wire transport

The socket is `AF_UNIX/SOCK_STREAM`. One client is accepted at a time.
Messages are framed by newline, matching the existing serial host writer:

```text
K28i2Z\n
n60l1i2Z\n
n60l0i2Z\n
```

A message may contain multiple AMY `Z`-terminated events if the normal AMY
parser accepts that payload; the socket framing itself only cares about the
newline.

Transport limits are intentionally fixed:

- maximum complete line: 1023 bytes plus NUL
- command ring: 256 messages
- oversized messages: discarded as a unit
- full ring: new message discarded

Dropped-message count is logged when the engine shuts down.

## Building

Requirements:

- JDK 17
- Android SDK platform 36
- Android NDK 27.0.12077973
- CMake 3.22.1
- Gradle 8.13

From the repository root:

```bash
cd android
gradle :amy-service:assembleDebug
```

The AAR is produced below:

```text
android/amy-service/build/outputs/aar/
```

The module uses the Oboe 1.10.0 Prefab dependency from Google's Maven
repository.

## Host transport regression test

The socket server and SPSC ring are plain POSIX C and can be tested without an
Android toolchain:

```bash
make -C android host-test
```

The test verifies:

- socket node mode `0600`
- ordered message delivery
- multiple messages from one stream read
- a message split over multiple writes
- CRLF handling
- oversized-line rejection/counting
- socket pathname cleanup on shutdown

## Embedding in the Qt Android application

Package the `amy-service` AAR into the Qt APK. Its manifest contributes the
unexported `org.amy.audio.AmyService` in the `:amy` process.

The Android host starts the service with `AmyService.start(context)`. This uses:

```text
<Context.getFilesDir()>/amy.sock
```

The Qt/Python transport then connects to that pathname with a Unix stream
socket and writes the same AMY wire lines used by the Raspberry Pi serial
transport.

The Qt integration belongs in the Omnichord repository; this AMY module knows
nothing about Qt, Python, presets, or Omnichord UI policy.

## Current scope

This branch implements one-way AMY wire command input. The stream socket is
bidirectional by construction, so the compact AMY introspection response path
can be added without changing IPC architecture once that AMY branch is merged
or rebased here.

The first hardware tests should measure:

1. touch-to-audio latency
2. Oboe callback size and device buffer size
3. xrun/glitch behavior during patch changes and heavy reverb/delay patches
4. service suspend/resume and audio-device changes

Patch allocation and other unusually heavy AMY commands are deliberately not
moved to a second AMY thread in this first version: maintaining a single owner
for AMY state avoids cross-thread locking in the normal render path. Hardware
measurements should determine whether those rare control operations need a
separate preparation mechanism.

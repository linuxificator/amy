# AMY Android Oboe service

This directory builds a generic Android AAR that hosts AMY in an unexported
`:amy` service process. The service owns Oboe/AAudio output and receives native
AMY wire messages through the private pathname Unix transport implemented by
`src/amy_unix_socket.[ch]`.

```text
Android client process
    |
    | AF_UNIX / SOCK_SEQPACKET
    | <app filesDir>/amy.sock
    | one AMY wire message per packet
    v
Android :amy service process
    |
    +-- amy_unix_socket receiver thread
    +-- fixed 64-packet SPSC queue
    +-- AMY C engine
    +-- Oboe low-latency callback
            |
            v
          AAudio
```

The AAR is embedded in an Android application package. Its private
`AmyAutoStartProvider` starts the separate `:amy` service process as part of
Android package initialization; client application code does not start or stop
AMY. A client can therefore be Java/Kotlin, native code, Godot, Qt, another
framework, or any other environment that can package an Android AAR and open an
Android Unix-domain `SOCK_SEQPACKET` socket. No AMY headers, AMY source, JNI
bindings, or language-specific AMY API are required in the client code.

The service declaration uses `android:exported="false"` and
`android:process=":amy"`. Consequently the service runs in a separate process
from the client while remaining in the same Android application package and
under the same application UID.

The service only accepts the exact pathname `<Context.getFilesDir()>/amy.sock`.
The native transport creates that node mode `0600` and additionally verifies
accepted peers with `SO_PEERCRED` against the service effective UID. The AAR
must therefore be packaged into the same application/UID as the client; this is
intentional and preserves the private-socket security model. See
`docs/android_unix_socket.md` for the transport/security contract.

## Audio profile

The Android native build uses AMY's existing 48 kHz / 128-frame build profile
and defines `AMY_NO_MINIAUDIO`; Oboe is the sole audio backend.

The marker-gated CI capture records eight seconds from both AMY's rendered
samples and the exact buffer handed to Oboe. This leaves a packaged framework
runtime enough startup time before UI-driven notes while remaining a one-shot,
test-only path; ordinary applications never allocate the capture buffers.

Oboe requests:

- stereo signed 16-bit output
- 48 kHz
- `PerformanceMode::LowLatency`
- `SharingMode::Exclusive`
- callback-driven output

The callback size is not assumed to equal 128 frames. The native adapter keeps
only the unconsumed tail of the current AMY block and calls
`amy_simple_fill_buffer()` exactly when another AMY block is required. It does
not add an extra 128-frame output ring.

Before each new AMY block the callback drains up to 64 already-queued socket
packets and passes them to `amy_add_message()`. The socket thread itself never
calls AMY and never participates in audio rendering.

AMY is started with its internal platform audio disabled and with AMY rendering
owned by the Oboe callback thread. The current Android build configuration
reserves 336 addressable oscillators, 11 runtime buses, 16 Karplus-Strong
oscillators, 1024 stored nested patterns, 64 events per pattern and 32 active
or pending pattern instances. These are service-host capacities, not
wire-protocol extensions; AMY's portable defaults remain smaller:
clients continue to send ordinary AMY messages and may use any smaller layout.

## JNI boundary

JNI exists only inside the service implementation. `AmyService` calls the
native library to start and stop AMY/Oboe and to report its actual Oboe output
device. Musical control never crosses JNI: notes, patches, sequencer commands,
and other control are unchanged AMY wire packets sent through `amy.sock`.

The client-facing architecture is deliberately transport-only:

```text
client application -> amy.sock -> AMY/Oboe service
```

The minimal Java hello-world demonstrates this literally with Android's public
`LocalSocket(SOCKET_SEQPACKET)` API. It neither imports `AmyService` nor loads a
native client library.

## Socket client contract

Use `AF_UNIX` + `SOCK_SEQPACKET` and send one logical AMY request per packet.
For example the payload of three consecutive packets may be:

```text
K28i2Z
n60l1i2Z
n60l0i2Z
```

Do not add stream framing or depend on newline boundaries. Packet boundaries
are preserved by `SOCK_SEQPACKET`.

The pathname also serves as the engine readiness boundary. `amy.sock` is not
created until Oboe has started and the realtime audio callback has executed at
least once. A client may therefore retry `connect()` while the service starts;
once `connect()` succeeds it may begin sending AMY wire packets immediately.
No fixed Android-startup sleep is required.

The socket is bidirectional. The Android engine currently consumes ordinary AMY
wire commands; the existing `amy_unix_socket_send()` path is ready for compact
introspection/status replies when that functionality is integrated.

## Client integration

A client application needs to:

1. package the `amy-service` AAR/module in the Android application;
2. obtain the application's actual private files directory rather than
   hard-code `/data/user/...`;
3. retry an `AF_UNIX` / `SOCK_SEQPACKET` connection to `<filesDir>/amy.sock`
   until the service publishes its ready socket;
4. send one ordinary AMY wire message per packet;
5. optionally receive response packets over the same bidirectional socket;
6. reconnect cleanly when its own Android/application lifecycle requires it.

Starting AMY is deliberately absent from the client contract. The packaged AAR
owns that Android lifecycle responsibility.

## Building the AAR

Requirements used by CI:

- JDK 17
- Android SDK platform 36
- Android NDK 27.2.12479018 (r27c)
- CMake 3.22.1
- Gradle 8.13
- Android Gradle Plugin 8.13.2
- Oboe 1.10.0 (Prefab dependency)

From the repository root:

```bash
cd android
gradle :amy-service:assembleDebug
```

The production Android service build targets `arm64-v8a`. Output is below:

```text
android/amy-service/build/outputs/aar/
```

## Tests

The private socket regression test is:

```bash
bash tests/run_amy_unix_socket_test.sh
```

It validates packet round-trip, mode/ownership, `EMSGSIZE` behavior,
oversized-packet rejection, cleanup, and protection against deleting an
existing non-socket path. `tests/test_android_service_contract.py` additionally
guards the AAR's private-process manifest, socket-only client boundary, and the
336-oscillator/11-bus/1024-pattern integration profile without requiring an
Android SDK.

`.github/workflows/android.yml` runs that regression plus a complete Android
AAR/NDK/Oboe build and emulator end-to-end test. The emulator arms its own
one-shot audio-capture marker before starting the client; the hello-world
application itself remains transport-only.

## Downstream PySide6 package findings

The service AAR at immutable build commit
`1e81ea571294c6aed8e2c0d57a9e09786561e9cf` from this release branch was also
packaged and released in the downstream
[LB Omnichord Android application][lb-android-package]. That client is useful
as a framework-integration reference, but its Qt and Python packaging
workarounds are not part of AMY's portable service contract.

The successful package used Python 3.11 and the official
`pyside6-android-deploy` command with matching PySide6 and shiboken6 6.11.2
Android wheels. The command generated the Qt deployment files and
`buildozer.spec`; the downstream build then added this AAR and its Oboe Prefab
dependency to the generated Gradle package. Qt's command uses
Buildozer/python-for-android as host-side packaging tools. Kivy is not an
application or runtime dependency and is not included in the APK.

Those tools were reproducible only as one pinned set: Android SDK 36, NDK
27.2.12479018, python-for-android commit
`3762c88c56e3443efb8eba2a02a2604b680240fd`, and Cython 0.29.36. The build also
had to expose the modern SDK manager at Buildozer 1.5's expected legacy path
and add python-for-android's local `libs` directory to Gradle repositories so
the AAR supplied with `--add-aar` could be resolved. The package regression
checks the requested AAR and wheel ABIs, verifies that the APK contains the
AMY/Oboe and matching CPython/shiboken libraries, and rejects an accidental
in-process `c_amy` or `libamy.so` frontend binding.

On Android the Qt client discovers the application-private files directory
with `QStandardPaths` and appends `amy.sock`; it does not hard-code an Android
user or `/data/user/...` path. The frontend and unexported `:amy` service then
remain separate processes under the same application UID.

python-for-android extracts its private Python/Qt payload on first launch. In
an emulator that extraction can consume a measured audio window, and an
occasional Qt/JNI startup race can terminate that first process. The downstream
test therefore retries only an unmeasured extraction warm-up, force-stops the
whole package, and keeps the subsequent measured UI/audio launch single-shot.
This avoids hiding failures in the behavior under test.

A Linux-hosted emulator may print a host PulseAudio (`pa`) warning even though
the Android application never uses PulseAudio. The downstream gate separately
requires the guest service to report Oboe/AAudio, captures the signed-16-bit
samples rendered by AMY and handed to Oboe, and requires them to match exactly.
It also checks non-silence and clipping independently. Its `-26 dBFS` floor is
specific to LB Omnichord's deliberate `V1` master limit (20 dB below AMY's raw
`V10` unity setting) plus 6 dB of patch/phase headroom; it is not a general AMY
test threshold.

[LB Omnichord release R20260830T153747][lb-release] passed that packaged
PySide6 test with 384000 stereo frames at 48 kHz, no clipping, and zero sample
mismatches between AMY and Oboe. Its arm64 APK is CI debug-signed for sideload
and emulator testing, not for a store or stable update channel. Physical
touchscreen, speaker, route-change and latency validation remains outstanding.

## Hardware-test items

The first device tests should measure:

1. command-to-audio latency;
2. negotiated Oboe callback/device buffer sizes;
3. xruns during patch changes and heavy reverb/delay loads;
4. suspend/resume and audio-device changes;
5. whether executing rare heavy AMY commands at a block boundary needs further
   separation from the realtime callback.

[lb-android-package]: https://github.com/linuxificator/LB_Omnichord/blob/f8724328b2e679533c7f3b97cee939e009b7eba7/amysynth_version/qt_frontend/packaging/android/README.md
[lb-release]: https://github.com/linuxificator/LB_Omnichord/releases/tag/R20260830T153747

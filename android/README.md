# AMY Android Oboe service

This directory builds a generic Android AAR that hosts AMY in an unexported
`:amy` service process. The service owns Oboe/AAudio output and receives native
AMY wire messages through the private pathname Unix transport implemented by
`src/amy_unix_socket.[ch]`.

The AAR also contains a small reusable `org.amy.audio.AmyClient`. `AmyClient`
does **not** contain or instantiate AMY: its native library only owns a
persistent `AF_UNIX` / `SOCK_SEQPACKET` descriptor in the caller process.

```text
Android / framework client process
    |
    +-- AmyClient
    |      |
    |      | AF_UNIX / SOCK_SEQPACKET
    |      | <app filesDir>/amy.sock
    |      | one AMY wire request per packet
    |      v
    |
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

The client may be an Android SDK application, Kotlin/Java, native code, Godot,
Qt or another framework. AMY itself has no dependency on the client UI
framework. For the Android-specific Godot design and rationale, see
`../godot/ANDROID.md`.

The service declaration uses `android:exported="false"` and
`android:process=":amy"`. Consequently the service runs in a separate process
from the client while remaining in the same Android application package and
under the same application UID.

The service only accepts the exact pathname `<Context.getFilesDir()>/amy.sock`.
The native transport creates that node mode `0600` and additionally verifies
accepted peers with `SO_PEERCRED` against the service effective UID. See
`docs/android_unix_socket.md` for the transport/security contract.

## Why keep AMY in a separate process?

The separate process is primarily an isolation boundary, not CPU reservation.
Linux/Android schedules threads, and `android:process=":amy"` does not reserve a
CPU core. The realtime-relevant scheduling comes from Oboe/AAudio's callback
thread.

The separation is still useful for realtime audio applications:

- UI/framework rendering, scripting and garbage collection do not run in the
  AMY process;
- large framework allocations do not share the AMY process heap;
- the synth/audio lifecycle has one clear owner;
- the same AMY/Oboe implementation is reused by every Android UI framework;
- clients send compact control packets, not rendered PCM, across the process
  boundary.

This is particularly useful for engines such as Godot: Android musical control
can use Godot's normal high-level AMY message builder while AMY renders directly
on the Oboe callback rather than feeding a Godot `AudioStreamGenerator` queue.
See `../godot/ANDROID.md` for the complete comparison.

## Audio profile

The Android native build uses AMY's existing 48 kHz / 128-frame build profile
and defines `AMY_NO_MINIAUDIO`; Oboe is the sole audio backend.

Oboe requests:

- stereo signed 16-bit output;
- 48 kHz;
- `PerformanceMode::LowLatency`;
- `SharingMode::Exclusive` (a request; Android may negotiate shared mode);
- callback-driven output.

The callback size is not assumed to equal 128 frames. The native adapter keeps
only the unconsumed tail of the current AMY block and calls
`amy_simple_fill_buffer()` exactly when another AMY block is required. It does
not add an extra 128-frame output ring.

AMY's 128-frame block is its synthesis/command quantum (about 2.67 ms at
48 kHz), not Android's hardware DMA size. Oboe reports the actual device burst
and buffer capacity; Android/AAudio/HAL owns the hardware-facing buffering.

Before each new AMY block the callback drains up to 64 already-queued socket
packets and passes them to `amy_add_message()`. The socket receiver thread itself
never calls AMY and never participates in audio rendering.

AMY is started with its internal platform audio disabled and with AMY rendering
owned by the Oboe callback thread. The current Android build configuration
reserves 16 Karplus-Strong oscillators.

## JNI boundaries

There are two intentionally small native boundaries:

1. `AmyService` uses JNI for AMY/Oboe lifecycle and diagnostics inside the
   separate service process.
2. `AmyClient` uses JNI only for Android Unix `SOCK_SEQPACKET` operations in the
   caller process, because Android's Java `LocalSocket` API is stream-oriented.

Musical commands are **not** JNI calls into AMY. A `sendWire()` JNI call only
writes one opaque AMY wire packet to `amy.sock`; parsing, scheduling and
rendering all remain in the service process.

The client-facing architecture is therefore deliberately transport-oriented:

```text
client framework -> AmyClient -> amy.sock -> AMY/Oboe service
```

## Reusable AmyClient API

Java/Kotlin/framework code can use:

```java
AmyService.start(context);

AmyClient client = new AmyClient();
int rc = client.connect(context); // one immediate attempt

// Convenience form for a worker thread:
rc = client.connectWithRetry(context, 5000);

client.sendWire("v0w0V10.0Z");
client.sendWire("v0n60l1Z");
client.sendWire("v0l0Z");

client.close();
AmyService.stop(context);
```

`connectWithRetry()` sleeps between attempts and therefore belongs on a worker
thread. Frameworks with their own asynchronous scheduler (for example Godot)
can instead call immediate `connect()` repeatedly without blocking their UI
thread.

After a successful connect the native descriptor is switched to non-blocking
mode. A full socket therefore produces a negative errno such as `-EAGAIN`
instead of blocking a UI/framework control thread.

The connection is intended to remain open for the lifetime of musical control;
do not open/close a Unix socket for every note.

## Socket client contract

The underlying transport is `AF_UNIX` + `SOCK_SEQPACKET`, one logical AMY
request per packet. Example payloads of three consecutive packets:

```text
K28i2Z
n60l1i2Z
n60l0i2Z
```

Do not add stream framing or depend on newline boundaries. Packet boundaries are
preserved by `SOCK_SEQPACKET`.

The pathname also serves as the engine-readiness boundary. `amy.sock` is not
created until Oboe has started and the realtime audio callback has executed at
least once. A client may therefore retry `connect()` while the service starts;
once `connect()` succeeds it may begin sending AMY wire packets immediately.
No fixed Android-startup sleep is required.

The socket is bidirectional. The Android engine currently consumes ordinary AMY
wire commands; the existing `amy_unix_socket_send()` path is available for
compact introspection/status replies when that functionality is integrated.

## Framework integration

A client application needs to:

1. package the `amy-service` AAR/module in the Android application;
2. start `org.amy.audio.AmyService` while synthesis is required;
3. create one persistent `AmyClient` (or implement the equivalent native socket
   contract if there is a specific reason not to use the provided client);
4. retry connection until `amy.sock` exists;
5. send one ordinary AMY wire request per packet;
6. optionally receive future response packets over the same bidirectional
   socket;
7. reconnect cleanly across service/audio lifecycle events.

The ordinary Android hello-world demonstrates the reusable Java client. The
Godot Android hello-world demonstrates the same AAR from GDScript through
`JavaClassWrapper`, without embedding AMY in the Godot process.

## Building the AAR

Requirements used by CI:

- JDK 17
- Android SDK platform 36
- Android NDK 27.0.12077973
- CMake 3.22.1
- Gradle 8.13
- Android Gradle Plugin 8.13.2
- Oboe 1.10.0 (Prefab dependency)

From the repository root:

```bash
cd android
gradle :amy-service:assembleDebug
```

The AAR contains native libraries for the configured ABIs and is written below:

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
existing non-socket path.

`.github/workflows/android.yml` builds the AAR and ordinary Android hello-world,
runs two clean emulator launches, verifies all C-scale wire packets, captures
AMY and Oboe PCM, checks their sample-for-sample identity and verifies healthy
digital output level without clipping.

`.github/workflows/godot-android.yml` additionally builds a real Godot Android
Gradle APK and checks the complete path:

```text
GDScript -> JavaClassWrapper -> AmyClient -> amy.sock -> :amy -> Oboe/AMY
```

## Hardware-test items

Useful device measurements remain:

1. command-to-audio latency;
2. negotiated Oboe callback/device burst and buffer sizes;
3. xruns during patch changes and heavy reverb/delay loads;
4. suspend/resume and audio-device changes;
5. whether rare heavy AMY commands at a block boundary need further separation
   from the realtime callback.

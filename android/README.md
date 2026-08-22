# AMY Android Oboe service

This directory builds an Android AAR that hosts AMY in an unexported `:amy`
service process. The service owns Oboe/AAudio output and receives native AMY
wire messages through the private pathname Unix transport implemented by
`src/amy_unix_socket.[ch]`.

```text
Qt/Python process
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

The AAR is intended to be packaged inside the same APK as the Qt application.
The service declaration uses `android:exported="false"` and
`android:process=":amy"`, so the two processes have the same application UID
but separate process heaps and runtimes.

The service only accepts the exact pathname `<Context.getFilesDir()>/amy.sock`.
The native transport creates that node mode `0600` and additionally verifies
accepted peers with `SO_PEERCRED` against the service effective UID. See
`docs/android_unix_socket.md` for the transport/security contract.

## Audio profile

The Android native build uses AMY's existing 48 kHz / 128-frame build profile
and defines `AMY_NO_MINIAUDIO`; Oboe is the sole audio backend.

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
owned by the Oboe callback thread. The Android configuration reserves 16
Karplus-Strong oscillators so the Omnichord Physical Strings program has the
same intended capacity as the ESP32-P4 target.

## JNI boundary

JNI is lifecycle glue only. `AmyService` calls the native library to start and
stop AMY/Oboe with the validated socket pathname. Notes, patches, sequencer
commands and other musical control do not cross JNI; they use the unchanged AMY
wire protocol through `amy.sock`.

That keeps the application-level architecture symmetric:

```text
Raspberry Pi: Qt/Python -> UART       -> ESP32-P4 AMY
Android:      Qt/Python -> amy.sock   -> local AMY/Oboe service
```

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

The socket is bidirectional. The Android engine currently consumes ordinary AMY
wire commands; the existing `amy_unix_socket_send()` path is ready for compact
introspection/status replies when the introspection branch is integrated.

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

The current build targets `arm64-v8a`. Output is below:

```text
android/amy-service/build/outputs/aar/
```

## Tests

The already-existing private socket regression test is:

```bash
bash tests/run_amy_unix_socket_test.sh
```

It validates packet round-trip, mode/ownership, `EMSGSIZE` behavior,
oversized-packet rejection, cleanup, and protection against deleting an
existing non-socket path.

`.github/workflows/android.yml` runs that regression plus a complete Android
AAR/NDK/Oboe build. The earlier `.github/workflows/android-unix-socket.yml`
continues to isolate the transport regression itself.

## Qt integration

The Qt/Python-side Android adapter belongs in `LB_Omnichord`, not in AMY. It
needs to:

1. start `org.amy.audio.AmyService` while the activity is foreground;
2. obtain the app's actual private files directory rather than hard-code
   `/data/user/...`;
3. connect a `SOCK_SEQPACKET` Unix socket to `<filesDir>/amy.sock`;
4. send exactly the same AMY wire payloads currently sent over UART;
5. stop/reconnect cleanly across Android activity/audio lifecycle events.

## Hardware-test items

The first device tests should measure:

1. touch-to-audio latency;
2. negotiated Oboe callback/device buffer sizes;
3. xruns during patch changes and heavy reverb/delay loads;
4. suspend/resume and audio-device changes;
5. whether executing rare heavy AMY commands at a block boundary needs further
   separation from the realtime callback.

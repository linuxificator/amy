# AMY Android Hello World

Minimal Android application proving the generic AMY Android service and reusable
client API end to end.

On launch it:

1. starts `org.amy.audio.AmyService` from the `amy-service` AAR/module;
2. uses the AAR's reusable `org.amy.audio.AmyClient` to retry a connection to
   the app-private `<filesDir>/amy.sock` `AF_UNIX` / `SOCK_SEQPACKET` socket
   until the AMY/Oboe service publishes its ready socket;
3. configures raw oscillator 0 as a sine wave and sets AMY global output gain to
   `V10.0`;
4. waits 30 ms so that setup is committed on a fresh AMY instance before the
   first note-on;
5. sends AMY wire commands for C4, D4, E4, F4, G4, A4, B4 and C5;
6. shows and logs `C scale complete` when all packets have been sent.

The application does not contain AMY and does not implement an app-specific JNI
socket bridge. `AmyClient` is part of the same generic AAR as `AmyService`. Its
small native library only owns the Unix `SOCK_SEQPACKET` descriptor; the AMY
engine and Oboe audio stream remain in the separate `:amy` service process.
Each `AmyClient.sendWire()` call is one ordinary AMY wire packet.

The generic AMY Android service also logs Oboe's actual output device ID and
resolves it through `AudioDeviceInfo`, so device logs identify routes such as
`BUILTIN_SPEAKER`, `BUILTIN_EARPIECE`, Bluetooth, wired headphones or USB where
Android exposes a matching device.

## Reusable client API

A normal Java/Kotlin/framework client can use the same API demonstrated here:

```java
AmyService.start(context);

AmyClient client = new AmyClient();
int rc = client.connect(context); // one immediate attempt
// Or, from a worker thread:
rc = client.connectWithRetry(context, 5000);

client.sendWire("v0w0V10.0Z");
client.sendWire("v0n60l1Z");
client.sendWire("v0l0Z");

client.close();
AmyService.stop(context);
```

The connection is intended to remain open for musical control. After connect,
the native client descriptor is non-blocking; a saturated control path returns
a negative errno rather than stalling a framework/UI thread.

`amy.sock` is not published until Oboe has started and its first callback has
run, so a successful `connect()` is the engine-readiness boundary. A client may
retry connection instead of sleeping a guessed service-start delay.

## Wire sequence

Setup:

```text
v0w0V10.0Z
```

`V` is AMY's bus/master output-volume control, not an oscillator-local amplitude
control. AMY's final mixer scales this 0..10 control by 0.1, so `V10.0` selects
full master gain for this audible hello-world test. `V2.0`, used by an earlier
version of this example, was only 20% linear master gain (about -14 dB relative
to `V10.0`).

Notes use MIDI note numbers and velocity, e.g. middle C:

```text
v0n60l1Z
v0l0Z
```

The complete scale is MIDI notes `60, 62, 64, 65, 67, 69, 71, 72`.

## Build

From `android/`:

```bash
gradle :hello-world:assembleDebug
```

APK:

```text
hello-world/build/outputs/apk/debug/hello-world-debug.apk
```

The CI Android emulator smoke test builds the AAR/APK and performs two clean
install/launch cycles. Each cycle must show exactly one AMY/Oboe startup, an
output-route diagnostic, exactly one completed C scale, all eight note-on
packets and no socket failure. The Android audio-level regression also captures
the raw AMY signed-16-bit render stream and the exact signed-16-bit callback
buffer handed to Oboe, verifies that they are sample-for-sample identical, and
checks their measured peak/RMS level.

The integration worker/client are deliberately not tied to a single Activity
instance: Android may recreate an Activity during startup or a configuration
change, and that must not interrupt a musical command sequence already in
progress.

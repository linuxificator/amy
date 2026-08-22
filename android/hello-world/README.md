# AMY Android Hello World

Minimal Android application proving the AMY Android service end to end.

On launch it:

1. starts `org.amy.audio.AmyService` from the `amy-service` AAR/module;
2. connects to the app-private `<filesDir>/amy.sock` Unix-domain `SOCK_SEQPACKET` socket;
3. configures raw oscillator 0 as a sine wave;
4. sends AMY wire commands for C4, D4, E4, F4, G4, A4, B4, C5;
5. shows `C scale complete` when all packets have been sent.

The note path does not call AMY through JNI. JNI is used only for the Android client-side Unix socket syscalls because the Java `LocalSocket` API is stream-oriented. The synth process receives ordinary AMY wire packets exactly as another AMY wire transport would.

## Wire sequence

Setup:

```text
v0w0V0.30Z
```

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

The CI Android emulator smoke test installs this APK, starts the activity, and requires both `AMY/Oboe started` and `C scale complete` in logcat.

# AMY Godot Android hello world

This project proves the Android service architecture end to end from GDScript:

```text
Godot GDScript
  -> Amy.send(Dictionary)
  -> Amy.message() wire serialization
  -> AmyAndroid.send_raw()
  -> JavaClassWrapper
  -> org.amy.audio.AmyClient (AAR)
  -> AF_UNIX / SOCK_SEQPACKET
  -> separate :amy Android process
  -> Oboe realtime callback
  -> AMY
  -> AAudio
```

AMY is deliberately **not** compiled into the Godot process. The example uses
the normal high-level Godot API (`send`, `message`, AMY parameter names) while
only replacing the Android transport/audio backend.

See `../ANDROID.md` for the architecture rationale, realtime implications and
integration guidance.

## Requirements

The example is written for Godot 4.7.2 and requires a Gradle Android export.
Godot 4.4+ provides `JavaClassWrapper` and `AndroidRuntime`; 4.7.2 is the current
stable version used by CI for this example.

The AMY AAR requires Android API 26 or later. CI builds both `arm64-v8a` and
`x86_64`; the latter is used by the Android emulator test.

## Prepare the example

From the repository root:

```bash
bash godot/android-hello-world/prepare.sh
```

This builds `android/amy-service` and copies three generated/runtime inputs into
`godot/android-hello-world/addons/amy_android/`:

- `amy-service-debug.aar`
- the existing high-level `godot/amy.gd`
- `godot/amy_android.gd`, the Android service backend subclass

The generated copies and AAR are gitignored; `godot/amy.gd` remains the single
source for the dictionary-to-wire API.

## Export from Godot

1. Open `godot/android-hello-world/project.godot` in Godot 4.7.2.
2. Install the Android Gradle Build template (`Project -> Install Android Build Template`).
3. Ensure an Android SDK/JDK is configured.
4. Export the existing `Android` preset as a debug APK.

The preset uses Gradle Build because Godot automatically includes `.aar` files
found under the project's `addons` directory only in Gradle exports.

Equivalent CI-style command after export templates are installed:

```bash
godot --headless \
  --path godot/android-hello-world \
  --install-android-build-template \
  --export-debug Android build/godot-amy-hello.apk
```

## What the example tests

On launch the GDScript code:

1. loads `AmyAndroid`, which inherits the existing `Amy` GDScript API;
2. starts `AmyService` through `JavaClassWrapper`;
3. creates `AmyClient` and retries the private socket until Oboe is actually running;
4. uses dictionaries to configure sine oscillator 0 at `volume=10`;
5. plays MIDI notes 60, 62, 64, 65, 67, 69, 71 and 72;
6. prints every serialized wire request and `Godot C scale complete`.

The GitHub Actions emulator test verifies the service/Oboe startup, Godot client
readiness, the eight note-on messages and successful completion. This is not a
mock transport test: it exercises GDScript -> JavaClassWrapper -> AAR JNI client
-> private socket -> separate AMY/Oboe process on Android.

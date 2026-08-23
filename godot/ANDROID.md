# AMY + Godot on Android: separate service architecture

## Recommendation

For Android, use AMY as the `amy-service` AAR in its own `:amy` process and let
Godot be a control/UI client over the private AMY wire socket. Do not embed a
second AMY renderer in the Godot process unless an application specifically
needs in-process audio for reasons that outweigh Android latency/isolation.

This is Android-specific advice. The existing embedded GDExtension remains a
reasonable desktop/native Godot backend, and the existing WASM backend remains
appropriate for the web.

## Why this differs from the normal Godot integration

The existing generic native Godot backend looks approximately like this:

```text
Godot process
  -> Amy GDScript
  -> AmySynth GDExtension
  -> AMY renderer
  -> Godot AudioStreamGenerator
  -> Godot/Android audio output
```

That design is portable and simple. The current generic wrapper also uses a
44.1 kHz `AudioStreamGenerator` with a 0.1 second generator buffer. For a game
that may be acceptable; for a touch musical instrument it puts Godot's audio
queue directly in the latency path and mixes AMY's scheduling fate with the
rendering/scripting process.

The Android service backend instead uses:

```text
Godot UI/game process
  -> Amy.send(Dictionary)
  -> existing Amy.message() wire serializer
  -> AmyAndroid.send_raw()
  -> JavaClassWrapper / AndroidRuntime
  -> AmyClient from amy-service.aar
  -> private AF_UNIX / SOCK_SEQPACKET amy.sock

separate :amy process (same application UID)
  -> socket receiver / bounded command queue
  -> Oboe high-priority low-latency audio callback
  -> amy_simple_fill_buffer()
  -> AAudio / device low-latency path
```

No rendered PCM is transferred between Godot and AMY. Godot sends compact
control messages only. AMY renders directly on Oboe's callback thread in the
process that owns the Android audio stream.

## Why the process separation is useful

The separate process is primarily **isolation**, not CPU reservation. Android
and Linux schedule threads, and `android:process=":amy"` does not reserve a CPU
core for AMY. The realtime-relevant thread priority comes from the callback
stream requested through Oboe/AAudio.

The separation is still valuable:

- Godot frame rendering, GDScript, scene changes and Java-side UI work are not
  running inside the AMY process.
- UI garbage collection and large framework allocations do not share the AMY
  process heap.
- The synth/audio lifecycle has one clear owner instead of being coupled to a
  Godot `AudioStreamGenerator` producer.
- An AMY service failure is isolated from most framework state, and framework
  changes do not require redesigning the synth engine.
- The Android scheduler sees a small audio-oriented process whose important
  work is the Oboe callback rather than a process containing the entire game/UI.
- The same service can be used by Godot, Qt, Java/Kotlin, a native Android app,
  or another framework without changing AMY's audio backend.

Again, this does not guarantee exclusive CPU time. Other Android/system work can
still preempt the audio callback.

## Realtime path and buffering

The service requests from Oboe:

- `PerformanceMode::LowLatency`
- `SharingMode::Exclusive` (a request; the device may negotiate Shared)
- callback-driven stereo signed-16-bit audio
- AMY's native 48 kHz rate

AMY is compiled at 48 kHz with 128-frame render blocks (2.67 ms per block).
Those 128 frames are the AMY synthesis/command quantum; they are **not** the
Android hardware DMA size. Android hides the hardware DMA behind AAudio/HAL and
reports its own device burst/buffer sizes through Oboe.

The adapter accepts whatever `numFrames` Android requests, consumes the tail of
the current AMY block and renders another 128-frame block only when necessary.
It deliberately does not add another full AMY output ring between the synth and
Oboe.

The service therefore keeps the low-latency Android audio path separate from
Godot's audio mixer and generator buffering.

## Why use the AMY wire protocol

AMY already has a compact wire representation for musical control. It is a good
process boundary because:

- messages are tiny compared with audio buffers;
- `SOCK_SEQPACKET` preserves exactly one logical AMY request per packet;
- no extra stream framing or JSON parser is needed in the realtime service;
- the service can queue messages and apply them at AMY block boundaries;
- the same representation is usable from any client language/framework.

The socket is private to the application. It lives at
`<Context.getFilesDir()>/amy.sock`, mode 0600; the server also checks
`SO_PEERCRED` and only accepts the same application UID. The service is
`android:exported="false"`. The `:amy` process has a different PID/address space
but the same Android application UID as Godot.

## Reusable AmyClient AAR API

The AAR exposes two framework-independent Java classes:

```java
AmyService.start(context);

AmyClient client = new AmyClient();
int rc = client.connect(context);              // one immediate attempt
// or from a worker thread:
rc = client.connectWithRetry(context, 5000);

client.sendWire("v0w0V10");
client.sendWire("v0n60l1");
client.sendWire("v0l0");

client.close();
AmyService.stop(context);
```

`AmyClient` loads a small native library containing only Unix socket operations;
it does **not** link or instantiate AMY in the client process. The connection is
persistent. The native descriptor is switched to non-blocking mode after
connect so a full control socket cannot stall a UI/framework thread; callers
receive a negative errno such as `-EAGAIN` instead.

`amy.sock` is deliberately not published until Oboe has started and its first
audio callback has run. Successful connection is therefore the engine-ready
boundary; clients retry connection instead of sleeping a guessed startup time.

## Godot API: keep the existing message builder

Do not make Godot users hand-write wire strings for normal use. `godot/amy.gd`
already mirrors AMY's Python-style API:

```gdscript
amy.send({
    "osc": 0,
    "wave": Amy.SINE,
    "freq": 440,
    "vel": 1.0,
})
```

and internally serializes the dictionary to the AMY wire protocol. The Android
backend in `godot/amy_android.gd` inherits that class and overrides only
lifecycle and `send_raw()`.

The resulting platform split is:

```text
                       high-level Amy GDScript API
                                  |
                 Amy.send(...) / Amy.message(...)
                                  |
             +--------------------+--------------------+
             |                    |                    |
          desktop                web                Android
             |                    |                    |
       GDExtension AMY         WASM AMY           AmyClient AAR
                                                      |
                                                   amy.sock
                                                      |
                                                :amy / Oboe AMY
```

The Android hello-world uses this exact dictionary API; the test does not bypass
it with a special Godot-only native binding.

## Why JavaClassWrapper instead of a Godot Android plugin

Godot 4.4 introduced `JavaClassWrapper` and the built-in `AndroidRuntime`
singleton. With a Gradle Android export, Godot automatically packages `.aar`
files found below the project's `addons` directory. For this small API, a custom
Godot Android plugin would mostly duplicate a bridge Godot already provides.

A Godot project therefore only needs the AAR plus the GDScript wrappers. The
hello-world's `prepare.sh` demonstrates the layout:

```text
addons/amy_android/
    amy-service-debug.aar
    amy.gd
    amy_android.gd
```

Then `AmyAndroid` calls `AmyService` and `AmyClient` directly through
`JavaClassWrapper`.

## Service configuration versus musical control

Musical control (notes, patches, oscillator parameters, effects commands,
sequencer messages) uses the normal AMY wire protocol and belongs to clients.
Engine-start configuration belongs to the service because there is exactly one
AMY engine for the application.

The first Android service profile is intentionally fixed at 48 kHz / 128 frames,
no audio input, no startup bleep and 16 reserved KS oscillators. Some exported
configuration properties inherited from the generic `Amy` GDScript class are
therefore not Android startup controls yet. If applications need configurable
engine creation, extend `AmyService` with an explicit service configuration
object rather than silently starting separate AMY instances per client.

## Lifecycle and ownership

The current service accepts one connected wire client. A typical Godot app
should:

1. create one `AmyAndroid` node near application startup;
2. allow it to start `AmyService` and connect once;
3. keep that socket for all musical control;
4. close the client and stop the service when the owning node/application exits.

If a future application needs multiple independent UI/control producers, add a
single client-side dispatcher or deliberately extend the server protocol rather
than opening competing socket clients accidentally.

## Testing strategy

There are three useful layers:

1. `tests/run_amy_unix_socket_test.sh` validates the transport independently.
2. The ordinary Android hello-world uses the reusable `AmyClient` and validates
   AAR -> socket -> AMY/Oboe, including captured digital audio level.
3. The Godot Android hello-world builds a real Godot Gradle APK and validates on
   an Android emulator that GDScript -> `JavaClassWrapper` -> `AmyClient` ->
   `:amy` -> Oboe reaches a complete C-major scale.

This makes the Godot layer thin and testable while keeping the audio-critical
implementation identical for every Android framework.

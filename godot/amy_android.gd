class_name AmyAndroid
extends Amy
## Android service backend for the high-level AMY GDScript API.
##
## The inherited Amy.send()/Amy.message() API still constructs ordinary AMY
## wire messages. Only send_raw() and lifecycle are replaced: messages are sent
## through the amy-service AAR to AMY running in the separate :amy process.
##
## Godot talks to the framework-neutral AmyAndroidBridge using only a Context,
## Strings and primitive return values. No AMY engine or rendered PCM exists in
## the Godot process.

const CONNECT_RETRIES: int = 120
const CONNECT_RETRY_SECONDS: float = 0.05
const ERR_JAVA_EXCEPTION: int = -1002

var _android_runtime: Object = null
var _android_context: Object = null
var _android_bridge: Object = null
var _android_last_error: int = 0
var _android_status: String = "Not started"

func _ready() -> void:
	if OS.get_name() != "Android":
		_set_status("AmyAndroid is only available in Android exports")
		push_warning(_android_status)
		return
	# Do not perform JNI/reflection work synchronously inside add_child(). This
	# lets the host scene render and exposes the exact startup stage if Android
	# or a third-party framework bridge ever stalls.
	call_deferred("_init_android")

func _set_status(text: String) -> void:
	_android_status = text
	print("Godot AMY stage: %s" % text)

func _java_exception(stage: String) -> bool:
	var exception: Object = JavaClassWrapper.get_exception()
	if exception == null:
		return false
	_android_last_error = ERR_JAVA_EXCEPTION
	_set_status("%s: Java exception" % stage)
	push_error("AMY Android %s raised Java exception: %s" % [stage, str(exception)])
	return true

func _bridge_error_text() -> String:
	if _android_bridge == null:
		return ""
	var value: Variant = _android_bridge.getLastErrorText()
	if _java_exception("getLastErrorText"):
		return "Java exception while reading bridge error"
	return str(value)

func _init_android() -> void:
	_set_status("Finding AndroidRuntime...")
	await get_tree().process_frame
	_android_runtime = Engine.get_singleton("AndroidRuntime")
	if _android_runtime == null:
		_android_last_error = -1
		_set_status("AndroidRuntime singleton unavailable")
		push_error("AMY Android: %s" % _android_status)
		return

	_set_status("Getting Android application Context...")
	await get_tree().process_frame
	_android_context = _android_runtime.getApplicationContext()
	if _android_context == null:
		_android_last_error = -1
		_set_status("Android application Context unavailable")
		push_error("AMY Android: %s" % _android_status)
		return

	_set_status("Loading AmyAndroidBridge class...")
	await get_tree().process_frame
	_android_bridge = JavaClassWrapper.wrap("org.amy.audio.AmyAndroidBridge")
	if _java_exception("wrap AmyAndroidBridge"):
		return
	if _android_bridge == null or not _android_bridge.has_java_method("start"):
		_android_last_error = -1
		_set_status("AmyAndroidBridge class/method unavailable")
		push_error("AMY Android: %s" % _android_status)
		return

	_set_status("Starting AMY Android service...")
	await get_tree().process_frame
	var start_value: Variant = _android_bridge.start(_android_context)
	if _java_exception("AmyAndroidBridge.start"):
		return
	var start_result: int = int(start_value)
	if start_result != 0:
		_android_last_error = start_result
		_set_status("AMY service start failed %d: %s" % [start_result, _bridge_error_text()])
		push_error("AMY Android: %s" % _android_status)
		return

	_set_status("AMY service requested; waiting for audio/socket...")
	for attempt in range(CONNECT_RETRIES):
		var result_value: Variant = _android_bridge.connect(_android_context)
		if _java_exception("AmyAndroidBridge.connect"):
			return
		var result: int = int(result_value)
		if result == 0:
			_android_last_error = 0
			_started = true
			_set_status("AMY ready")
			print("AMY Android service ready")
			return
		_android_last_error = result
		if attempt == 0 or (attempt + 1) % 20 == 0:
			_set_status("Waiting for amy.sock (attempt %d/%d, rc=%d)" % [attempt + 1, CONNECT_RETRIES, result])
		await get_tree().create_timer(CONNECT_RETRY_SECONDS).timeout

	_set_status("AMY socket timeout rc=%d: %s" % [_android_last_error, _bridge_error_text()])
	push_error("AMY Android: %s" % _android_status)

# The Android service/Oboe path owns audio. Never feed Godot's
# AudioStreamGenerator on this backend.
func _process(_delta: float) -> void:
	pass

func _exit_tree() -> void:
	_started = false
	if _android_bridge != null:
		_android_bridge.stop(_android_context)
		_java_exception("AmyAndroidBridge.stop")
	_android_bridge = null

func is_running() -> bool:
	return _started

func last_error() -> int:
	return _android_last_error

func status_text() -> String:
	return _android_status

## Send one ordinary AMY wire request as one SOCK_SEQPACKET packet.
func send_raw(msg: String) -> void:
	if not _started or msg.is_empty() or _android_bridge == null:
		return
	var result_value: Variant = _android_bridge.sendWire(msg)
	if _java_exception("AmyAndroidBridge.sendWire"):
		_started = false
		return
	var result: int = int(result_value)
	if result < 0:
		_android_last_error = result
		_started = false
		_set_status("AMY send failed %d: %s" % [result, _bridge_error_text()])
		push_error("AMY Android send failed: %d" % result)

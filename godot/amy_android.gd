class_name AmyAndroid
extends Amy
## Android service backend for the high-level AMY GDScript API.
##
## The inherited Amy.send()/Amy.message() API still constructs ordinary AMY
## wire messages. Only send_raw() and lifecycle are replaced: messages are sent
## through the amy-service AAR to AMY running in the separate :amy process.

const CONNECT_RETRIES: int = 100
const CONNECT_RETRY_SECONDS: float = 0.05

var _android_runtime: Object = null
var _android_context: Object = null
var _android_service: Object = null
var _android_client: Object = null
var _android_last_error: int = 0

func _ready() -> void:
	if OS.get_name() != "Android":
		push_warning("AmyAndroid is only available in Android exports")
		return
	_init_android()

func _init_android() -> void:
	_android_runtime = Engine.get_singleton("AndroidRuntime")
	if _android_runtime == null:
		_android_last_error = -1
		push_error("AMY Android: AndroidRuntime singleton unavailable")
		return

	_android_context = _android_runtime.getApplicationContext()
	if _android_context == null:
		_android_last_error = -1
		push_error("AMY Android: application Context unavailable")
		return

	var service_class: Object = JavaClassWrapper.wrap("org.amy.audio.AmyService")
	var client_class: Object = JavaClassWrapper.wrap("org.amy.audio.AmyClient")
	_android_service = service_class
	_android_client = client_class.AmyClient()
	_android_service.start(_android_context)

	for _attempt in range(CONNECT_RETRIES):
		var result: int = int(_android_client.connect(_android_context))
		if result == 0:
			_android_last_error = 0
			_started = true
			print("AMY Android service ready")
			return
		_android_last_error = result
		await get_tree().create_timer(CONNECT_RETRY_SECONDS).timeout

	push_error("AMY Android: unable to connect to amy.sock, error %d" % _android_last_error)

# The Android service/Oboe path owns audio. Never feed Godot's
# AudioStreamGenerator on this backend.
func _process(_delta: float) -> void:
	pass

func _exit_tree() -> void:
	_started = false
	if _android_client != null:
		_android_client.close()
		_android_client = null
	if _android_service != null and _android_context != null:
		_android_service.stop(_android_context)

func is_running() -> bool:
	return _started

func last_error() -> int:
	return _android_last_error

## Send one ordinary AMY wire request as one SOCK_SEQPACKET packet.
func send_raw(msg: String) -> void:
	if not _started or msg.is_empty() or _android_client == null:
		return
	var result: int = int(_android_client.sendWire(msg))
	if result < 0:
		_android_last_error = result
		_started = false
		push_error("AMY Android send failed: %d" % result)

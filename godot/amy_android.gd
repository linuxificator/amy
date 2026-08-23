class_name AmyAndroid
extends Amy
## Android transport for the high-level AMY GDScript API.
##
## Amy.send()/Amy.message() stay in GDScript and produce ordinary AMY wire
## messages. Android starts the separate :amy service with the application;
## this class only connects to amy.sock and sends those wire messages.

const CONNECT_RETRIES: int = 120
const CONNECT_RETRY_SECONDS: float = 0.05

var _android_context: Object = null
var _android_client: Object = null
var _android_last_error: int = 0
var _android_status: String = "Waiting for amy.sock"

func _ready() -> void:
	if OS.get_name() != "Android":
		push_warning("AmyAndroid is only available in Android exports")
		return
	call_deferred("_connect_android")

func _connect_android() -> void:
	var runtime: Object = Engine.get_singleton("AndroidRuntime")
	if runtime == null:
		_fail("AndroidRuntime unavailable", -1)
		return

	_android_context = runtime.getApplicationContext()
	if _android_context == null:
		_fail("Android application Context unavailable", -1)
		return

	_android_client = JavaClassWrapper.wrap("org.amy.audio.AmyClient")
	if _android_client == null or not _android_client.has_java_method("connect"):
		_fail("AmyClient unavailable", -1)
		return

	for attempt in range(CONNECT_RETRIES):
		var result: int = int(_android_client.connect(_android_context))
		var exception: Object = JavaClassWrapper.get_exception()
		if exception != null:
			_fail("AmyClient.connect Java exception: %s" % str(exception), -1000)
			return
		_android_last_error = result
		if result == 0:
			_started = true
			_android_status = "AMY ready"
			print("AMY Android service ready")
			return
		_android_status = "Waiting for amy.sock (rc=%d)" % result
		await get_tree().create_timer(CONNECT_RETRY_SECONDS).timeout

	_fail("AMY socket timeout (rc=%d)" % _android_last_error, _android_last_error)

func _fail(text: String, error: int) -> void:
	_android_status = text
	_android_last_error = error
	push_error("AMY Android: %s" % text)

# AMY/Oboe owns audio in the separate Android process.
func _process(_delta: float) -> void:
	pass

func _exit_tree() -> void:
	_started = false
	if _android_client != null:
		_android_client.close()
	_android_client = null

func is_running() -> bool:
	return _started

func last_error() -> int:
	return _android_last_error

func status_text() -> String:
	return _android_status

## Send one ordinary AMY wire request as one SOCK_SEQPACKET packet.
func send_raw(msg: String) -> void:
	if not _started or msg.is_empty() or _android_client == null:
		return
	var result: int = int(_android_client.sendWire(msg))
	var exception: Object = JavaClassWrapper.get_exception()
	if exception != null:
		_started = false
		_fail("AmyClient.sendWire Java exception: %s" % str(exception), -1000)
		return
	if result < 0:
		_started = false
		_fail("AMY send failed: %d" % result, result)

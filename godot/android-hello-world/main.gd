extends Control

var _status: Label
var _play_button: Button
var _amy_client: Object
var _android_context: Object

func _ready() -> void:
	_build_ui()

	if OS.get_name() != "Android":
		_fail("Android export required")
		return

	var runtime: Object = Engine.get_singleton("AndroidRuntime")
	if runtime == null:
		_fail("AndroidRuntime unavailable")
		return

	_android_context = runtime.getApplicationContext()
	if _android_context == null:
		_fail("Android application context unavailable")
		return

	_amy_client = JavaClassWrapper.wrap("org.amy.audio.AmyClient")
	if _amy_client == null:
		_fail("AmyClient class unavailable")
		return
	if not _amy_client.has_java_method("connect") or not _amy_client.has_java_method("sendWire"):
		_fail("AmyClient methods unavailable")
		return

	_status.text = "Connecting to amy.sock..."
	for _attempt in range(200):
		var rc: int = int(_amy_client.connect(_android_context))
		var exception: Object = JavaClassWrapper.get_exception()
		if exception != null:
			_fail("AmyClient.connect exception: %s" % str(exception))
			return
		if rc == 0:
			print("Godot connected to amy.sock")
			_status.text = "Connected to AMY"
			_play_button.disabled = false
			await _play_scale()
			return
		await get_tree().create_timer(0.05).timeout

	_fail("Could not connect to amy.sock")

func _exit_tree() -> void:
	if _amy_client != null:
		_amy_client.close()

func _build_ui() -> void:
	var center := CenterContainer.new()
	center.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(center)

	var column := VBoxContainer.new()
	column.custom_minimum_size = Vector2(680, 0)
	column.alignment = BoxContainer.ALIGNMENT_CENTER
	center.add_child(column)

	var title := Label.new()
	title.text = "AMY + Godot raw-wire proof of concept"
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title.add_theme_font_size_override("font_size", 28)
	column.add_child(title)

	_status = Label.new()
	_status.text = "Initializing..."
	_status.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_status.add_theme_font_size_override("font_size", 20)
	column.add_child(_status)

	_play_button = Button.new()
	_play_button.text = "Play C major scale"
	_play_button.disabled = true
	_play_button.pressed.connect(_on_play_pressed)
	column.add_child(_play_button)

func _on_play_pressed() -> void:
	await _play_scale()

func _send_wire(wire: String) -> bool:
	var rc: int = int(_amy_client.sendWire(wire))
	var exception: Object = JavaClassWrapper.get_exception()
	if exception != null:
		_fail("AmyClient.sendWire exception: %s" % str(exception))
		return false
	if rc != 0:
		_fail("amy.sock send failed: %d" % rc)
		return false
	print("Godot wire: %s" % wire)
	return true

func _play_scale() -> void:
	_play_button.disabled = true
	_status.text = "Playing C major scale..."

	# Stage 1 deliberately uses the exact literal AMY wire commands from the
	# already-working Android hello-world. No Amy.gd/API translation is involved.
	if not _send_wire("v0w0V10.0Z"):
		return
	await get_tree().create_timer(0.03).timeout

	for note in [60, 62, 64, 65, 67, 69, 71, 72]:
		if not _send_wire("v0n%dl1Z" % note):
			return
		await get_tree().create_timer(0.35).timeout
		if not _send_wire("v0l0Z"):
			return
		await get_tree().create_timer(0.08).timeout

	_status.text = "C scale complete"
	_play_button.disabled = false
	print("Godot raw-wire C scale complete")

func _fail(message: String) -> void:
	push_error(message)
	print("Godot raw-wire error: %s" % message)
	if _status != null:
		_status.text = message
	if _play_button != null:
		_play_button.disabled = true

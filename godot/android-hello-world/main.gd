extends Control

const AmyApi = preload("res://amy.gd")

var _status: Label
var _play_button: Button
var _amy
var _amy_error: String = ""

func _ready() -> void:
	_build_ui()

	if OS.get_name() != "Android":
		_fail("Android export required")
		return

	_status.text = "Connecting Amy.gd to amy.sock..."
	_amy = AmyApi.new()
	_amy.debug_wire = true
	_amy.backend_ready.connect(_on_amy_ready)
	_amy.backend_error.connect(_on_amy_error)
	add_child(_amy)

func _build_ui() -> void:
	var center := CenterContainer.new()
	center.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(center)

	var column := VBoxContainer.new()
	column.custom_minimum_size = Vector2(680, 0)
	column.alignment = BoxContainer.ALIGNMENT_CENTER
	center.add_child(column)

	var title := Label.new()
	title.text = "AMY + Godot high-level API proof"
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

func _on_amy_ready() -> void:
	print("Godot Amy.gd Android backend ready")
	_status.text = "Connected to AMY through Amy.gd"
	_play_button.disabled = false
	await _play_scale()

func _on_amy_error(message: String) -> void:
	_amy_error = message
	_fail(message)

func _on_play_pressed() -> void:
	await _play_scale()

func _send(params: Dictionary) -> bool:
	_amy_error = ""
	var wire: String = _amy.message(params)
	print("Godot Amy.gd message: %s" % wire)
	_amy.send(params)
	return _amy_error.is_empty()

func _play_scale() -> void:
	_play_button.disabled = true
	_status.text = "Playing C major scale through Amy.gd..."

	# Stage 2 uses only Amy.gd's ordinary dictionary API. The shared message()
	# implementation produces the wire command and Amy.gd's Android backend sends
	# exactly that command through the private service socket.
	if not _send({"osc": 0, "wave": AmyApi.SINE, "volume": 10.0}):
		return
	await get_tree().create_timer(0.03).timeout

	for note in [60, 62, 64, 65, 67, 69, 71, 72]:
		if not _send({"osc": 0, "note": note, "vel": 1.0}):
			return
		await get_tree().create_timer(0.35).timeout
		if not _send({"osc": 0, "vel": 0.0}):
			return
		await get_tree().create_timer(0.08).timeout

	_status.text = "C scale complete"
	_play_button.disabled = false
	print("Godot Amy.gd C scale complete")

func _fail(message: String) -> void:
	push_error(message)
	print("Godot Amy.gd error: %s" % message)
	if _status != null:
		_status.text = message
	if _play_button != null:
		_play_button.disabled = true

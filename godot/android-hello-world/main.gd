extends Control

var _amy: Node = null
var _status: Label = null
var _play_button: Button = null

func _ready() -> void:
	_build_ui()
	_status.text = "Starting AMY Android service..."

	var android_backend: Script = load("res://addons/amy_android/amy_android.gd")
	if android_backend == null:
		_fail("AMY Android addon missing; run prepare.sh before export")
		return

	_amy = android_backend.new()
	add_child(_amy)

	for _attempt in range(120):
		if _amy.call("is_running"):
			_status.text = "AMY ready"
			print("Godot AMY ready")
			await _play_scale()
			return
		await get_tree().create_timer(0.05).timeout

	_fail("AMY service connection timeout: %s" % str(_amy.call("last_error")))

func _build_ui() -> void:
	var center := CenterContainer.new()
	center.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(center)

	var column := VBoxContainer.new()
	column.custom_minimum_size = Vector2(560, 0)
	column.alignment = BoxContainer.ALIGNMENT_CENTER
	center.add_child(column)

	var title := Label.new()
	title.text = "AMY Godot Android Hello World"
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title.add_theme_font_size_override("font_size", 30)
	column.add_child(title)

	_status = Label.new()
	_status.text = "Initializing..."
	_status.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_status.add_theme_font_size_override("font_size", 20)
	column.add_child(_status)

	_play_button = Button.new()
	_play_button.text = "Play C scale"
	_play_button.disabled = true
	_play_button.pressed.connect(_on_play_pressed)
	column.add_child(_play_button)

func _on_play_pressed() -> void:
	_play_button.disabled = true
	await _play_scale()

func _send_and_log(params: Dictionary) -> void:
	var wire: String = str(_amy.call("message", params))
	print("Godot wire: %s" % wire)
	_amy.call("send", params)

func _play_scale() -> void:
	if _amy == null or not _amy.call("is_running"):
		_fail("AMY is not connected")
		return

	_play_button.disabled = true
	_status.text = "Playing C major scale..."

	# Use the same high-level dictionary API as desktop/web Godot. The inherited
	# Amy.message() serializes these dictionaries; AmyAndroid only changes the
	# transport backend to the private Android service.
	_send_and_log({"osc": 0, "wave": 0, "volume": 10.0})
	await get_tree().create_timer(0.03).timeout

	for note in [60, 62, 64, 65, 67, 69, 71, 72]:
		_send_and_log({"osc": 0, "note": note, "vel": 1.0})
		await get_tree().create_timer(0.35).timeout
		_send_and_log({"osc": 0, "vel": 0.0})
		await get_tree().create_timer(0.08).timeout

	_status.text = "C scale complete"
	_play_button.disabled = false
	print("Godot C scale complete")

func _fail(message: String) -> void:
	push_error(message)
	print("Godot AMY error: %s" % message)
	if _status != null:
		_status.text = message
	if _play_button != null:
		_play_button.disabled = true

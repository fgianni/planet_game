extends Node3D


func _ready() -> void:
    $Planet.rebuild(4, 6_371_000.0)


func _process(delta: float) -> void:
    $Planet.rotate_y(delta * 0.12)

extends Node3D

@export var simulated_hours_per_second: float = 2.0
@export var snapshot_updates_per_second: float = 12.0

var _pending_simulation_ticks: float = 0.0
var _time_since_snapshot_s: float = 0.0


func _ready() -> void:
    $Planet.rebuild(4, 6_371_000.0)


func _process(delta: float) -> void:
    _pending_simulation_ticks += delta * simulated_hours_per_second * 60.0
    _time_since_snapshot_s += delta
    var update_interval_s := 1.0 / maxf(snapshot_updates_per_second, 1.0)
    if _time_since_snapshot_s >= update_interval_s:
        var whole_ticks := floori(_pending_simulation_ticks)
        if whole_ticks > 0:
            $Planet.advance_simulation_ticks(whole_ticks)
            _pending_simulation_ticks -= whole_ticks
        _time_since_snapshot_s = 0.0

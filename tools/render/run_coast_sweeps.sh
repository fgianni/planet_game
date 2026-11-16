#!/bin/bash
# R1-05 rendered gate sweeps (ADR-0030 A1.6 task 4): L4 and L6, two seeds,
# two coastal foci, both styles, true-scale relief (A1.5b). Logs in
# build/r105/gate2/. Needs build/coast_render_reference.
GODOT=${GODOT:-godot4}
OUT=build/r105/gate2; mkdir -p $OUT
for level in 4 6; do for seed in 20260928 42; do
  foci=$(build/coast_render_reference foci $level $seed 2)
  n=0
  for focus in $foci; do
    for style in map stylised; do
      dir=$OUT/L${level}_s${seed}_f${n}_${style}
      rm -rf $dir
      timeout 1500 $GODOT --display-driver x11 --audio-driver Dummy --path godot --script res://tests/capture_coast_sweep.gd -- \
        --subdivision=$level --seed=$seed --style=$style --focus=$focus --exaggeration=1 \
        --altitudes=24000,12000,6000,3000,1500,750,375,188,94,47,23,12,6,3,1.5 --output=$PWD/$dir > $dir.godot.log 2>&1
      python3 tools/render/compare_coast_render.py $dir $level $seed --warmup 1 > $dir.compare.log 2>&1
      echo "$dir: $(tail -1 $dir.compare.log) ($(grep -c ERROR $dir.godot.log) Godot errors)"
    done
    n=$((n+1))
  done
done; done

#!/usr/bin/env python3
"""Measure the ADR-0018 V8 readability images with CIEDE2000."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
from typing import Iterable


THRESHOLD = 10.0

# Full-severity matrices from Machado, Oliveira and Fernandes (2009).
CVD_MATRICES = {
    "protanopia": (
        (0.152286, 1.052583, -0.204868),
        (0.114503, 0.786281, 0.099216),
        (-0.003882, -0.048116, 1.051998),
    ),
    "deuteranopia": (
        (0.367322, 0.860646, -0.227968),
        (0.280085, 0.672501, 0.047413),
        (-0.011820, 0.042940, 0.968881),
    ),
    "tritanopia": (
        (1.255528, -0.076749, -0.178779),
        (-0.078411, 0.930809, 0.147602),
        (0.004733, 0.691367, 0.303900),
    ),
}


def _read_ppm(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    tokens: list[bytes] = []
    offset = 0
    while len(tokens) < 4:
        while offset < len(data) and data[offset] in b" \t\r\n":
            offset += 1
        if data[offset : offset + 1] == b"#":
            offset = data.index(b"\n", offset) + 1
            continue
        end = offset
        while end < len(data) and data[end] not in b" \t\r\n":
            end += 1
        tokens.append(data[offset:end])
        offset = end
    if tokens[0] != b"P6" or tokens[3] != b"255":
        raise ValueError(f"{path}: expected binary 8-bit PPM")
    if offset >= len(data) or data[offset] not in b" \t\r\n":
        raise ValueError(f"{path}: missing PPM header terminator")
    offset += 1
    width, height = int(tokens[1]), int(tokens[2])
    pixels = data[offset:]
    if len(pixels) != width * height * 3:
        raise ValueError(f"{path}: expected {width * height * 3} pixel bytes, got {len(pixels)}")
    return width, height, pixels


def _linear_channel(value: float) -> float:
    value /= 255.0
    return value / 12.92 if value <= 0.04045 else ((value + 0.055) / 1.055) ** 2.4


def _rgb_to_lab(rgb: tuple[int, int, int], matrix=None) -> tuple[float, float, float]:
    linear = tuple(_linear_channel(channel) for channel in rgb)
    if matrix is not None:
        linear = tuple(
            min(1.0, max(0.0, sum(row[i] * linear[i] for i in range(3)))) for row in matrix
        )
    red, green, blue = linear
    x = (0.4124564 * red + 0.3575761 * green + 0.1804375 * blue) / 0.95047
    y = 0.2126729 * red + 0.7151522 * green + 0.0721750 * blue
    z = (0.0193339 * red + 0.1191920 * green + 0.9503041 * blue) / 1.08883
    delta = 6.0 / 29.0

    def f(value: float) -> float:
        return value ** (1.0 / 3.0) if value > delta**3 else value / (3.0 * delta**2) + 4.0 / 29.0

    fx, fy, fz = f(x), f(y), f(z)
    return 116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz)


def ciede2000(first: tuple[float, float, float], second: tuple[float, float, float]) -> float:
    l1, a1, b1 = first
    l2, a2, b2 = second
    c1, c2 = math.hypot(a1, b1), math.hypot(a2, b2)
    c_bar = (c1 + c2) / 2.0
    g = 0.5 * (1.0 - math.sqrt(c_bar**7 / (c_bar**7 + 25.0**7)))
    ap1, ap2 = (1.0 + g) * a1, (1.0 + g) * a2
    cp1, cp2 = math.hypot(ap1, b1), math.hypot(ap2, b2)

    def hue(a: float, b: float) -> float:
        angle = math.degrees(math.atan2(b, a))
        return angle + 360.0 if angle < 0.0 else angle

    hp1, hp2 = hue(ap1, b1), hue(ap2, b2)
    dl = l2 - l1
    dc = cp2 - cp1
    dh_angle = hp2 - hp1
    if cp1 * cp2 == 0.0:
        dh_angle = 0.0
    elif dh_angle > 180.0:
        dh_angle -= 360.0
    elif dh_angle < -180.0:
        dh_angle += 360.0
    dh = 2.0 * math.sqrt(cp1 * cp2) * math.sin(math.radians(dh_angle / 2.0))
    l_bar = (l1 + l2) / 2.0
    cp_bar = (cp1 + cp2) / 2.0
    if cp1 * cp2 == 0.0:
        hp_bar = hp1 + hp2
    elif abs(hp1 - hp2) <= 180.0:
        hp_bar = (hp1 + hp2) / 2.0
    elif hp1 + hp2 < 360.0:
        hp_bar = (hp1 + hp2 + 360.0) / 2.0
    else:
        hp_bar = (hp1 + hp2 - 360.0) / 2.0
    t = (
        1.0
        - 0.17 * math.cos(math.radians(hp_bar - 30.0))
        + 0.24 * math.cos(math.radians(2.0 * hp_bar))
        + 0.32 * math.cos(math.radians(3.0 * hp_bar + 6.0))
        - 0.20 * math.cos(math.radians(4.0 * hp_bar - 63.0))
    )
    sl = 1.0 + 0.015 * (l_bar - 50.0) ** 2 / math.sqrt(20.0 + (l_bar - 50.0) ** 2)
    sc = 1.0 + 0.045 * cp_bar
    sh = 1.0 + 0.015 * cp_bar * t
    rotation = 30.0 * math.exp(-((hp_bar - 275.0) / 25.0) ** 2)
    rc = 2.0 * math.sqrt(cp_bar**7 / (cp_bar**7 + 25.0**7))
    rt = -math.sin(math.radians(2.0 * rotation)) * rc
    return math.sqrt((dl / sl) ** 2 + (dc / sc) ** 2 + (dh / sh) ** 2 + rt * (dc / sc) * (dh / sh))


def _pixels(data: bytes, width: int, height: int, margin: int) -> Iterable[tuple[int, int, int]]:
    for y in range(margin, height - margin):
        for x in range(margin, width - margin):
            offset = 3 * (y * width + x)
            yield data[offset], data[offset + 1], data[offset + 2]


def _mean_delta(first: bytes, second: bytes, width: int, height: int, margin: int, variant: str) -> float:
    matrix = CVD_MATRICES.get(variant)
    total = 0.0
    count = 0
    for rgb1, rgb2 in zip(_pixels(first, width, height, margin), _pixels(second, width, height, margin)):
        lab1, lab2 = _rgb_to_lab(rgb1, matrix), _rgb_to_lab(rgb2, matrix)
        if variant == "luminance":
            lab1, lab2 = (lab1[0], 0.0, 0.0), (lab2[0], 0.0, 0.0)
        total += ciede2000(lab1, lab2)
        count += 1
    return total / count


def _self_test() -> None:
    # Sharma et al. (2005), supplementary test pair 1.
    measured = ciede2000((50.0, 2.6772, -79.7751), (50.0, 0.0, -82.7485))
    if abs(measured - 2.0425) > 0.0001:
        raise AssertionError(f"CIEDE2000 reference mismatch: {measured}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", nargs="?", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--threshold", type=float, default=THRESHOLD)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    _self_test()
    if args.self_test and args.input is None:
        print("CIEDE2000 reference test passed")
        return 0
    if args.input is None:
        parser.error("input manifest is required")

    manifest = json.loads(args.input.read_text(encoding="utf-8"))
    base_dir = args.input.parent
    variants = ("normal", "deuteranopia", "protanopia", "tritanopia", "luminance")
    results = []
    passed = True
    for pair in manifest["images"]:
        width1, height1, first = _read_ppm(base_dir / pair["baseline"])
        width2, height2, second = _read_ppm(base_dir / pair["changed"])
        if (width1, height1) != (width2, height2):
            raise ValueError("paired images have different dimensions")
        scores = {
            variant: _mean_delta(first, second, width1, height1, manifest["crop_margin"], variant)
            for variant in variants
        }
        pair_passed = all(value >= args.threshold for value in scores.values())
        passed = passed and pair_passed
        results.append({**pair, "mean_ciede2000": scores, "passed": pair_passed})
        print(f"{pair['style']:10} {pair['signal']:18} min ΔE00 {min(scores.values()):.2f}")

    report = {
        "schema": "PlanetSim.readability.v1",
        "render_input_schema": manifest["schema"],
        "image_size": [manifest["width"], manifest["height"]],
        "crop_margin": manifest["crop_margin"],
        "threshold_mean_ciede2000": args.threshold,
        "colour_vision_model": "Machado et al. 2009, severity 1.0",
        "results": results,
        "passed": passed,
    }
    output = args.output or base_dir / "readability.json"
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"readability {'passed' if passed else 'FAILED'}; analytics: {output}")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())

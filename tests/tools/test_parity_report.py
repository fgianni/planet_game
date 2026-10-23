#!/usr/bin/env python3
"""Check parity diagnostics, including default layers and budget identities."""

from __future__ import annotations

import math
import re
import subprocess
import sys


def measure(executable: str, explicit_layers: bool) -> tuple[dict, dict]:
    command = [executable, "parity", "--subdivision", "3", "--seed", "1",
               "--spin-up-years", "0", "--days", "1", "--workers", "1"]
    if explicit_layers:
        command += ["--layers", "3"]
    result = subprocess.run(command, capture_output=True, text=True, check=False)
    # This short diagnostic is deliberately not the eleven-year V8 gate.
    if result.returncode not in (0, 3):
        raise AssertionError(result.stdout + result.stderr)
    records = {}
    for mode in ("climate", "reference"):
        block = re.search(rf"^{mode}:.*(?:\n  .*)*", result.stdout, re.MULTILINE)
        assert block, result.stdout
        records[mode] = dict(re.findall(r"(\w+)=([^\s]+)", block[0]))
        # INV-04: land and ocean by 10° band, finite and in range.
        for name in ("land_air_K", "ocean_air_K", "land_air_q_g_kg", "ocean_air_q_g_kg"):
            values = re.search(rf"{name}=((?: [0-9.eE+\-]+)+)", block[0])
            assert values, block[0]
            numbers = [float(x) for x in values[1].split()]
            assert len(numbers) == 18 and all(math.isfinite(x) and x >= 0 for x in numbers)
            records[mode][name] = numbers
        for name in ("land_zonal_mean_K", "ocean_zonal_mean_K"):
            values = re.search(rf"{name}=((?: [0-9.eE+\-]+)+)", block[0])
            assert values, block[0]
            numbers = [float(x) for x in values[1].split()]
            assert len(numbers) == 18
            assert all(x == 0 or 150 < x < 350 for x in numbers), (name, numbers)
            records[mode][name] = numbers
        for name in ("vapour_kg_m2_by_layer", "layer_temperature_K"):
            values = re.search(rf"{name}=((?: [0-9.eE+\-]+)+)", block[0])
            assert values, block[0]
            numbers = [float(x) for x in values[1].split()]
            assert len(numbers) == 3 and all(math.isfinite(x) and x > 0 for x in numbers)
            records[mode][name] = numbers
        row = records[mode]
        number = lambda key: float(row[key])
        assert number("albedo_decomposition_error_W_m2") < 1e-9
        # Allow only the report's six-significant-digit rounding.
        assert abs(number("bare_clear_sky_absorbed_W_m2")
                   - number("snow_clear_sky_reflection_W_m2")
                   - number("ice_clear_sky_reflection_W_m2")
                   - number("clear_sky_absorbed_W_m2")) < 0.002
        assert abs(number("clear_sky_absorbed_W_m2") + number("cre_sw_W_m2")
                   - number("absorbed_W_m2")) < 0.002
        # Climate mode's transport moves heat and water between land and
        # ocean without creating either: the area-weighted sums vanish.
        if mode == "climate":
            land = number("land_fraction")
            for part in ("dry", "latent"):
                net = (land * number(f"land_transport_{part}_W_m2")
                       + (1 - land) * number(f"ocean_transport_{part}_W_m2"))
                scale = abs(land * number(f"land_transport_{part}_W_m2")) + 1e-9
                assert abs(net) <= 1e-4 * scale + 1e-6, (part, net)
        for kind in ("land", "ocean"):
            assert 0 <= number(f"{kind}_convective_share") <= 1
            assert number(f"{kind}_precipitation_m_yr") >= 0
        # The land tiles' budget closes, and the melt split is a share.
        assert abs(number("land_budget_residual_W_m2")) < 1e-6
        assert 0 <= number("melt_in_cold_months_share") <= 1
        assert number("melt_m_yr") >= 0
        fraction = number("land_fraction")
        assert abs(fraction * number("land_mean_K")
                   + (1 - fraction) * number("ocean_mean_K") - number("mean_K")) < 0.002
        for field in ("snow_cover_fraction_of_land", "ice_cover_fraction_of_ocean",
                      "energy_gate_ratio", "water_gate_ratio"):
            assert 0 <= number(field) <= 1, (field, row[field])
    gate_line = next(line for line in result.stdout.splitlines() if line.startswith("parity "))
    gate = dict(re.findall(r"(\w+)=([^\s]+)", gate_line))
    assert gate["precipitation_ratio_direction"] == "reference_over_climate"
    ratio = float(records["reference"]["precipitation_m_yr"]) / float(
        records["climate"]["precipitation_m_yr"])
    assert math.isclose(float(gate["precipitation_ratio"]), ratio, rel_tol=1e-5)
    assert (result.returncode == 0) == (gate["gate"] == "pass")
    return records, gate


def main() -> None:
    implicit, implicit_gate = measure(sys.argv[1], False)
    explicit, explicit_gate = measure(sys.argv[1], True)
    for mode in implicit:
        # Timing is not a simulation result; all other reported values,
        # including the slow-state hash, must be identical.
        implicit[mode].pop("wall_s")
        explicit[mode].pop("wall_s")
    assert implicit == explicit
    assert implicit_gate == explicit_gate
    print("parity report expectations passed")


if __name__ == "__main__":
    main()

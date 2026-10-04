# Zonal-mean circulation prototype (task M6-04, step A)

Python (numpy) scripts behind ADR-0011 §14. They are evidence for the
amendment, not production code. Run them on `--zonal-csv` output from
`planet_cli dynamics` or `planet_cli reference`, from this directory.

| Script | What it does |
|---|---|
| `closure.py FILE.csv` | Measured eddy diffusivities. A Green fit, and the momentum-flux convergence implied by PV diffusion through the Taylor identity, against the measured convergence. |
| `momentum.py FILE.csv` | Eddy momentum flux against ∂E/∂y, and eddy kinetic energy against \|∂θ̄/∂y\|². |
| `mixing.py FILE.csv...` | Heat flux against the mixing length `ℓ_h √(2E) σ²` and against Green's `c_e \|∂θ̄/∂y\|`. |
| `lm.py FILE.csv...` | The momentum length ℓ_m. |
| `zonal_model.py` | The axisymmetric model under Held–Suarez forcing, by explicit march or by Newton. |
| `compare.py FILE.csv "dict(...)"` | The model's march against a reference profile. |
| `newton_test.py FILE.csv` | Newton from the reference state. |
| `omega.py` | The rotation sweep (V8). |

The reference CSVs were produced with:

```bash
planet_cli dynamics --test held-suarez --subdivision 4 --layers 3 --days 400 \
    --average-days 300 --damping-hours 8 --zonal-csv hs_L4_N3.csv
planet_cli dynamics --test held-suarez --subdivision 4 --layers 5 --days 600 \
    --average-days 450 --damping-hours 8 --zonal-csv hs_L4_N5.csv
planet_cli reference --subdivision 4 --days 1096 --average-days 730 \
    --zonal-csv ref_L4_N3.csv
```

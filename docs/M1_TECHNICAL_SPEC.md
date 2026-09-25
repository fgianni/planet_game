# P0 / M1 Technical Specification

Status: implemented and validated, including ADR-0002/0003 migration

## Scope

M1 implements rotation, a fixed Keplerian orbit, axial tilt, solar direction,
day/night, seasons, and cell-centered top-of-atmosphere insolation. It does not
implement terrain, surface absorption, temperature, atmosphere, or an energy
solver.

## Coordinate and time conventions

- Planet mesh coordinates are right-handed and body-fixed.
- `+Z` is geographic north and the rotation axis.
- `+X` is the prime-meridian direction at the epoch.
- Positive rotation is eastward about `+Z`.
- Authoritative simulation time is a finite non-negative signed 64-bit tick
  count, with one tick equal to one simulated minute. SI seconds are derived
  from the tick and never accumulated.
- Orbital phase is mean anomaly measured from periapsis.
- At the epoch, the star direction is `+X` in both inertial equatorial and
  body-fixed coordinates.
- Mean anomaly increases uniformly. Eccentric anomaly is obtained from
  `E - e*sin(E) = M`, and true anomaly and orbital distance follow from it.
- The default longitude of periapsis is `pi`, making the epoch both
  periapsis and the northern vernal equinox. Other scenarios may choose a
  different orientation and initial mean anomaly.
- The rotation period is sidereal. Combining inertial rotation with the
  changing orbital star direction produces the solar day.

For solar longitude `lambda`, obliquity `epsilon`, and inertial rotation
angle `theta`, the unit star direction is:

```text
sun_inertial = (cos(lambda),
                cos(epsilon) * sin(lambda),
                sin(epsilon) * sin(lambda))
sun_body = rotate_about_z(sun_inertial, -theta)
```

## Parameters and resolution

Earth defaults use:

- radius: `6,371,000 m`;
- mass: `5.9722e24 kg`;
- sidereal rotation period: `86,164.0905 s`;
- tropical year: `31,556,925.216 s`;
- axial tilt: `23.439281 degrees`;
- semi-major axis: `149,597,870,700 m`;
- eccentricity: `0.0167086`;
- stellar luminosity chosen to give `1,361 W/m2` at the semi-major axis.

Under accepted ADR-0002, L5 is the development default with 10,242 dual cells,
and L6 is the shipped/reference target with 40,962 dual cells. Forcing is
stored on the authoritative dual cells; the primal triangles are private mesh
construction scaffolding.

## Solar forcing

Each cell samples insolation at its normalized center:

```text
distance_m = semi_major_axis_m * (1 - eccentricity * cos(E))
incident_solar_flux_W_m2 = star_luminosity_W / (4*pi*distance_m^2)
mu = max(0, dot(cell_center_unit, sun_body_unit))
top_of_atmosphere_insolation_W_m2 = incident_solar_flux_W_m2 * mu
```

The center sample is a documented finite-volume quadrature approximation; no
sub-cell terminator clipping is introduced in M1. Area-weighted global
diagnostics quantify its instantaneous error against `S(d)/4` and
`pi * R^2 * S(d)`.
At L5, the annual and instantaneous relative errors must remain below
`5e-4`; lower resolutions are diagnostic and are not held to that bound.

The forcing field, orbital angles, and solar directions are derived from
physical time and parameters. `PlanetState` owns the current derived field;
Godot receives a read-oriented versioned `StateSnapshot` and never computes
authoritative orbital physics.

## Required validation

- Parameter invariants reject non-finite or non-physical values.
- Kepler residuals remain within floating-point tolerance through
  eccentricity `0.95`.
- Epoch and quarter-orbit solar geometry match the coordinate convention.
- Rotation and orbital phase wrap deterministically.
- Night-side insolation is exactly zero and all values lie in `[0, S(d)]`.
- Equinox illumination is symmetric between equal north/south latitudes.
- Northern summer solstice favors northern over southern equal latitudes and
  illuminates the north pole while leaving the south pole dark.
- Periapsis and apoapsis distance and flux follow the expected inverse-square
  ratio.
- Area-weighted global mean incoming solar is approximately `S0/4` for a
  circular orbit, with the tolerance justified by cell-center quadrature.
- Repeated evaluation with identical inputs is exactly deterministic within a
  build.
- Evaluation and diagnostics are bit-identical with 1, 2, 8, and 16 workers,
  using fixed logical blocks and fixed-order reduction.
- The CLI reports solar geometry, global incoming power, quadrature error, and
  invalid/non-finite counts headlessly.
- Godot animates snapshots produced from PlanetSim physical time; presentation
  code does not own a separate rotation or season model.

## Local tangent frame

Latitude and longitude are derived from the authoritative surface-normal unit
vector. Each normal has a right-handed orthonormal `East/North/Up` basis for
later tangent wind, current, and slope vectors. Exact poles use a deterministic
`+Y` east convention because geographic east is undefined there.

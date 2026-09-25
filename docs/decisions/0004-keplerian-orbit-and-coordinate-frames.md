# ADR 0004: Keplerian orbit and coordinate frames

- Status: Accepted
- Date: 2026-09-24
- Milestone: P0 / M1

## Context

M1 must support Earth as a parameter set rather than a hard-coded special
case. The simulator needs day/night and seasons now, inverse-square forcing for
eccentric scenarios, and stable local frames for later winds, currents, and
terrain slopes. Rendering must not become the authority for any of these
quantities.

## Decision

PlanetSim uses three explicit coordinate concepts:

- an inertial equatorial frame for the fixed spin axis and orbital star
  direction;
- a rotating body frame containing the immutable icosphere and all cell
  fields;
- a derived right-handed local East/North/Up basis for each surface normal.

The M1 orbit is a fixed two-body Keplerian ellipse. Mean anomaly advances
uniformly with physical time. Eccentric anomaly is found with a deterministic
64-iteration bisection solution of Kepler's equation, then converted to true
anomaly, orbital distance, and solar longitude. Stellar flux is
`L / (4*pi*d^2)`.

The default longitude of periapsis is chosen so simulation time zero is both
periapsis and the northern vernal equinox. This is an epoch convention, not a
claim to reproduce Earth's historical calendar. Longitude of periapsis and
initial mean anomaly remain scenario parameters.

Local longitude and latitude are derived from the authoritative 3D unit
normal. East is the increasing-longitude tangent away from the poles. At
either exact pole, where geographic east is undefined, a deterministic
`+Y` east direction is used; north is then derived so `east × north = up`.

## Consequences

- Eccentric-orbit scenarios receive physical distance forcing from M1.
- The orbit has fixed elements: no precession, nutation, N-body perturbations,
  tidal evolution, or obliquity evolution is included.
- The bisection solve costs a fixed small amount per forcing update and is not
  performed per cell.
- Pole tangent orientation is conventional but finite, deterministic, and
  orthogonal.
- Godot consumes the resulting snapshot fields and owns only their visual
  mapping.

## Alternatives considered

### Circular orbit until a later milestone

This satisfies the original minimal M1 acceptance test but conflicts with the
expanded v0.2 M1 contract and prevents early inverse-square experiments.

### Newton iteration only

Usually faster, but requires initial-guess and convergence handling near high
eccentricity. Fixed bisection is simpler, bounded, and negligible at the M1
update cadence.

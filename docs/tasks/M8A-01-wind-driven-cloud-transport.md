# Proposed milestone M8A — Wind-driven cloud transport

- **Track:** P0 physical follow-up to M8, with a rendering integration check
- **Status:** proposed; no implementation authorized (2026-10-23)
- **Order:** after INV-04's current transport investigation, before starting this
  follow-up; M8 stays complete and M9 is not part of this task
- **Decision gate:** a new accepted ADR must resolve ADR-0023 §1.2 item 2 and
  §8 question 1 before changing the solver. It must choose the transport
  operator and cadence in each mode, how liquid-water enthalpy is accounted
  for, and whether the short condensate residence time justifies any
  approximation. No visual tuning is a substitute for that decision.
- **Boundaries:** no direct Godot dependency in PlanetSim, no invented cloud
  cover, no silent field-meaning or PSNAP change, and no change to V8's limits.

## Why this is separate from M8

M7 transports vapour; M8 condenses it into layered cloud water and renders
column cover and optical thickness. Reference-mode winds advect vapour and
climate mode transports humidity, but existing cloud water is not carried
horizontally. A pattern moving in the cloud shader would not change the
authoritative cloud water, precipitation or radiation. ADR-0023 explicitly
deferred condensate advection and asked whether its short lifetime makes it
necessary at L6. The answer needs a controlled experiment, not an animation.

## Implementation work packages, once the decision is accepted

1. **Baseline and decision evidence.** Measure layered cloud-water lifetime,
   displacement expected from resolved winds over that lifetime, and the
   no-transport budget at L4 and L6 in both modes. Compare a conservative
   advected prototype against the existing model in controlled flows before
   committing to an operator or a preset change. Record costs and differences
   in cloud fraction, precipitation and radiative effects; do not select by
   resemblance to a reference image alone.
2. **Physical transport.** Apply the accepted conservative operator to each
   layer's existing cloud-water state at the decided cadence and ordering,
   with the column's microphysics and radiation reading that same state.
   Couple moved liquid
   mass and its enthalpy consistently; bound non-negativity without creating
   or destroying water. Use reference-mode resolved winds and the accepted
   climate-mode circulation/transport representation, not a second set of
   visual-only winds. Keep worker-count-independent ordering and snapshot
   restart behavior.
3. **Presentation integration.** Derive `cloud_cover` and `cloud_thickness`
   from the transported state through the existing snapshot boundary. The
   styles may advect fine visual texture with the published wind channel
   between snapshots, but they may not move the cloud footprint or invent
   cloud in a cell whose authoritative cover and optical thickness are zero.
   Maintain clear/partial/overcast readability in both styles.

## Acceptance, to be finalized by the ADR

- A prescribed uniform flow translates a passive cloudy patch by the
  expected displacement in each layer; zero wind leaves it in place. A
  closed-domain test conserves cloud water (and water plus precipitation
  after microphysics), retains non-negative concentrations and closes the
  energy/enthalpy budget at the existing per-step tolerances.
- A controlled wind reversal reverses cloud-water transport and the resulting
  downstream cloud/rain response; a frozen-wind control does not. Diagnose
  cloud-water flux by layer and source/sink terms separately, so the effect
  is not mistaken for vapour transport or local condensation.
- Climate and reference modes remain on the same physical field definitions.
  Re-run ADR-0023 V1–V12, ADR-0001's worker-count determinism and V8 parity,
  L4/L6 budgets and restart, and the L5/L6 performance gates. Report any
  recalibration explicitly; do not relax a failed gate to land this feature.
- A saved-frame comparison at fixed seed and wind shows the cloud footprint
  follows the transported state in both styles, while changing only the
  camera, style or animation time leaves all state hashes unchanged. Zero
  cover/optical depth remains visually clear; the readability gate passes.

## Not in this milestone

Ice-cloud microphysics, invented Earth-like storm tracks, new vegetation or
hydrology, and M9. Wind-shaped appearance alone cannot close M8A. If the
baseline and ADR find advection unnecessary at this resolution, close the
proposal with that evidence rather than add a cosmetic solver term.

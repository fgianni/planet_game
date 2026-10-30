# ADR-0029 — Land's water in climate mode

- **Status:** Proposed (2026-10-29), for decision with the user
- **Date:** 2026-10-29
- **Milestone:** P0, between M8 and M9 (investigation INV-04)
- **Context documents:** `docs/tasks/INV-04-v8-transport-and-radiation.md`
  ("Climate mode's land is dry", "The water reaches land only through the
  eddies", "Water for land with the heat mixed", "ADR-0028 implemented")
- **Related:** ADR-0011 §17.6 (the azonal flow carries no heat), ADR-0021
  §4.5 (the humidity on the transport's groups), ADR-0023 §16 (V8 at L4),
  ADR-0024 (the eddies on moist enthalpy), ADR-0027 (the air mixed within
  the transport's groups), ADR-0028 (the equatorial waves' exchange);
  specification M9 (hydrology, rivers and lakes)

## 1. Context

**V8 passes at L4, but not because land has its water.** Under ADR-0028,
land and ocean temperatures agree between the modes (land 287.01 against
286.62 K, ocean 291.79 against 291.74 K). But climate mode's transport
still brings land almost no water:

| | Climate, L4 | Reference, L4 | Climate, L5 |
|---|---:|---:|---:|
| Latent heat delivered to land (W/m² of land) | 1.7 | 23.0 | 1.74 |
| Moisture convergence over land (m/yr) | about 0.02 | about 0.29 | about 0.02 |

Earth's land receives about 0.27 m/yr (its P − E). The L5 column is the
climate half of the V8 run in progress (`build/inv04/v8_L5_3yr.log`): land
precipitation 0.49 m/yr against land evaporation 33.0 W/m² (0.42 m/yr).
Climate land rains mostly its own evaporation back.

**What INV-04 measured before ADR-0028** (L4, ADR-0027's preset, climate
mode, about 18 s a run):

- **Water mixed within the groups** changes nothing: what is missing comes
  from between groups.
- **Ten times the eddies' water exchange** adds 2.6 W/m² to land and dries
  its air.
- **The zonal-mean zonal wind, or the azonal flow's column-mean fluxes,**
  add 1.3–1.4 W/m².
- **The azonal balance's full fluxes** (ADR-0011 §17.6, withdrawn for the
  heat) moisten the whole atmosphere rather than land. A 3 % share warms
  the planet 2.3 K, with land's delivery unchanged. At full share, land's
  delivery is 7.9 W/m², with the planet 16 K warmer.
- **The deficit is in the latent transport everywhere, not only on the way
  to land** (2026-10-24, before ADR-0028):
  - poleward at 30–50°, 0.34–0.57 PW against reference mode's
    1.49–2.50 PW;
  - at 10° S and 10° N it runs poleward (−0.24 and +0.14 PW), where
    reference mode's converges on the equator (+0.41 and −0.51 PW);
  - 98 % of climate mode's precipitation is convective, against 25 %.

At L5, climate mode's poleward latent transport still peaks at 0.45 PW
(30–40° N).

**Why it matters now.**
- **M9 is hydrology:** soil moisture, runoff, catchments and rivers. Its
  rivers are fed by land's P − E, which in climate mode is about a quarter
  of reference mode's and Earth's.
- **The V8 pass is a compensation.** The radiation fit makes up for the
  drier air with a larger dry optical depth (climate greenhouse 172.5
  against 159.2 W/m², INV-04). Over land, the cloud longwave effect is
  half of reference mode's (19.5 against 40.2 W/m² at L4 before ADR-0027;
  14.4 W/m² at L5). A later change to land's clouds or radiation can break
  the agreement.

## 2. Decision drivers

1. Land's water should come from the ocean through the atmosphere, as in
   reference mode, so that M9's runoff has a physical source.
2. V8 must keep passing at L4 (ADR-0023 §16), and V11's budgets must hold.
3. No scripted moisture source: the water has to be moved by the transport
   (the specification's physical-fluxes constraint).
4. The cost of each option has to be known before it is chosen. INV-04's
   climate-only probes take seconds; V8 takes about 3 hours at L4.

## 3. Options

**A. Find why the tropical latent transport runs the wrong way, and fix
that.** In reference mode, the Hadley cell's lower branch carries moist air
to the equator. Climate mode's overturning, at 10° S and 10° N, carries
latent heat poleward. If its bottom layer is not the moistest, or its
overturning's mass flux is weak or of the wrong vertical shape, then the
humidity it carries is the problem, not a missing path.
- First step: measure, by layer and band, the overturning's mass flux and
  the humidity it carries, in both modes (diagnostics only).
- Cost: hours. It may be a defect rather than a missing process.
- Risk: it may show that the overturning is right and the eddies are the
  issue, which leads back to B or C.

**B. A thermodynamically constrained azonal circulation.** This is
ADR-0011 §17.6's named revisit: a weak-temperature-gradient-like model, or
the azonal flow's rotational part, whose divergent flow is bounded by the
heating. That would give onshore flow, monsoons and stationary eddies.
- Cost: a milestone-sized task with its own refit and gates.
- Risk: INV-04 found that the unconstrained azonal fluxes moisten the
  whole atmosphere. A constrained form has to be shown to move water to
  land, not upward.

**C. Accept it as a P0 limitation and record it.** V8 passes, and M9 runs
on climate mode's land P − E as it is.
- M9's rivers would then be about a quarter of Earth's discharge, or M9
  could use reference mode's P − E only in its own validation.
- Cost: none now; the debt moves to M9 and M10 (vegetation's water).

## 4. Recommendation

**A first, as a measurement.** It costs hours, and it tests the one
finding INV-04 has not explained: the latent transport's wrong sign near
the equator. If it shows a defect, its fix is likely smaller than B. If it
shows the overturning is right, B or C is decided with that evidence.

## 5. Not decided here

- Which of A's possible fixes, if any, is taken: that would be an
  amendment or a new ADR, with its refit and V8.
- The deep ocean's drift (ADR-0023 §16.3), and V8 at L5 (the run in
  progress on 2026-10-29).

## 6. Option A measured (2026-10-30)

INV-04 ("ADR-0029 option A: each layer's fluxes", both modes at L4):
nothing in the climate transport runs the wrong way by a defect. Climate
mode's water stays in its bottom layer (all its tropical condensation
there, against half of reference mode's; a quarter of reference mode's
vapour above it), and its Hadley overturning is 4–10 times weaker, its
lower branch hardly converging on the equator. So the latent transport has
little water to carry and diverges from the equator. The deficit lies in
the climate column's vertical moisture and the overturning it drives:
ADR-0026's ground. The next step is a climate-only test (seconds at L4,
off by default) of whether more water and condensation aloft strengthen
the overturning and bring land its water; its result decides between an
amendment to ADR-0026 and options B and C here.

**The column sweep (2026-10-30, INV-04).** The climate air's coldness is
not set by Betts–Miller's RH_bm, the surface–air exchange or the radiation
split (2–3 K at most). ADR-0028's equatorial waves are the largest single
cause: without them the tropical air warms 2–7 K, holds 45–110 % more water
and the Hadley overturning converges on the equator (+18 / −17 Sv at
10° S / 10° N, against −4 / −13 with them); land's water rises only from
1.7 to 2.4 W/m², still a tenth of reference mode's. A remaining 4–5 K and
half the overturning have another cause.


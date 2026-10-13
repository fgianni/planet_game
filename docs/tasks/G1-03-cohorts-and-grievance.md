# Task G1-03 — Three cohorts, grievance, trust and protest

- **Track:** gameplay G1 (`docs/VERTICAL_SLICE.md` §5)
- **Status:** proposed (2026-10-11); needs G1-02
- **Scope:** replace the prototype's single `happy` value with three
  cohorts carrying needs met, grievance, trust and support, and the
  petition → strike → riot ladder.
- **Governing decisions:** `docs/VERTICAL_SLICE.md` §5; specification
  §29.6.3, §29.6.7, §29.6.8 (reduced)
- **Must not touch:** routes' physics from G1-02

## 1. Model

Data in `data/cohorts.json`: name, cells (by land use rule), weights of
each need. Per turn:

``` text
needs_met_c   = Σ w_i · satisfaction_i(c)          food, jobs, air, fish,
                                                     losses, treasury
grievance_c  += g · (1 − needs_met_c) + Σ visible harms in c's cells
                − decay · grievance_c − relief_share_c
support_c     = σ(α·needs_met_c + β·trust_c − γ·grievance_c)
legitimacy    = population-weighted mean of support_c
```

A harm is *visible* to a cohort when it happens in its own cells and its
source is a building or zone there (soot from a mill in the town, a
flooded valley, a dead bay). Harms with no visible source (the regional
drying) raise grievance through unmet needs only. This difference is
what Q1 and Q5 of the slice test.

## 2. Protest ladder

Grievance thresholds produce a petition, then a strike (output of the
cohort's cells −30 % for one turn), then a riot (−60 %, a building in
those cells damaged). Each event names its cohort, its cells and its
dominant grievance source, for the newspaper and the map.

## 3. Acceptance and report

- In the coal-expansion strategy, mill-town grievance rises before coast
  grievance; in the algae strategy, coast grievance rises first.
- Relief spending lowers grievance with a lag of at most one turn.
- Every protest event names a grievance source that exists in the state.
- Legitimacy replaces `happy` in the end conditions; "revolt" fires when
  legitimacy stays below 0.15 for two turns.
- Report the grievance curves of the four scripted strategies.

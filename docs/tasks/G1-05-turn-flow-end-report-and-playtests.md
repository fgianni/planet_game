# Task G1-05 — Turn flow, end report, logging and the playtest kit

- **Track:** gameplay G1 (`docs/VERTICAL_SLICE.md` §7–§9, §11)
- **Status:** proposed (2026-10-11); needs G1-04
- **Scope:** the seven-step turn of §7 in the slice scene, the
  consequence card, the end report and debrief, the decision log and
  survey, the balance runner and the playtest protocol.
- **Governing decisions:** `docs/VERTICAL_SLICE.md` §7–§9; specification
  §15.1, §30.11, §30.12
- **Must not touch:** slice state equations (G1-02 to G1-04)

## 1. Scene

- The seven steps of §7 in order, each skippable with one key.
- Cohort portraits: a mood line and one complaint sentence generated from
  the dominant grievance source.
- Consequence card: at most one per turn, a still of the affected cells
  from the region camera and a cause chain built from the event's record
  (the slice's consequence ledger).
- Records panel: hidden until *local records* shows data; the absent
  climate view is a designed state, introduced by one line of text
  (specification §17.2).

## 2. End report and debrief

The four indicators of `VERTICAL_SLICE.md` §8 as small charts, the
society's one-line history from its routes, the fingerprint from the
shadow planet, and, in a second page, the region's series next to Earth's
from a reference file (specification §30.12). That file does not exist
yet: create it as `prototype/climate_planet_0.5/data/reference/earth.csv`
with yearly CO2, global temperature anomaly and population from 1880,
and a source and licence per column. The slice reads it in presentation
only, and a test asserts that removing it leaves the game's state
unchanged.

## 3. Logging and survey

- Per turn: the levers changed, time spent on the turn, and an optional
  free-text note the player can type.
- After the end report: five questions, matching Q3–Q5 and Q8 of the slice
  ("What was this game about?", "Why did your society use the energy it
  used?", "What caused the worst thing that happened to you?", "Was the
  pace right?", one free comment).
- Export as one JSON file per game beside `export_log()`.

## 4. Balance runner

`tests/test_slice.gd -- --strategies` plays coal only, algae-seeking,
dam-seeking and research-heavy over the seeds listed by G1-02 and prints
the end indicators. No strategy may dominate on every seed type.

## 5. Playtest kit

`docs/playtests/slice-protocol.md`: five players, think-aloud, one game
each on assigned seeds, observer sheet keyed to Q1–Q8, and a summary
template that reports each question's measure against its pass line.

## 6. Acceptance and report

- A full game plays through the scene start to finish without a debug
  key.
- The exported JSON contains everything needed to compute Q2, Q3, Q6 and
  Q7 without the video.
- Report the balance runner's table.

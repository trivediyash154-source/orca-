# ORCA — Visual Design

Version 1.0 · Phase 10 · 2026-09-03

How ORCA's frontend turns real `SimulationSnapshot` data into the Mission Control
experience — the design system, the Earth/orbit/entity visual language, and the principle
that governs all of it: **every visible value traces to the engine; every visual effect is
either that value directly, or a labeled, deterministic presentation transformation of it.**

---

## 1. Visual Personality

Aerospace command center × scientific visualization × modern engineering UI — restrained,
never neon, never gamified. Glow and motion are used to encode meaning (a critical
emergency's pulse ring, a selected marker's halo), never as decoration. No texture assets
are vendored (none existed in this repository, and fetching one would be an unverified new
dependency for a detail the brief itself marks optional) — Earth, orbit rings, and markers
are built from geometry and the design tokens below, not photographs.

## 2. Design Tokens

`frontend/src/styles/tokens.css` — every color/spacing/radius/typography value used
anywhere in the frontend, defined once as a CSS custom property. No component hardcodes a
hex value.

| Token | Value | Used for |
|---|---|---|
| `--orca-bg` | `#05070d` | Page/scene background |
| `--orca-surface` / `-elevated` | `#0b1119` / `#121a26` | Panels, cards |
| `--orca-text` / `--orca-muted` | `#e6ecf5` / `#7f8fa6` | Primary/secondary text |
| `--orca-primary` | `#4a90d6` | Brand, satellite markers, LEO-adjacent accents |
| `--orca-success` | `#45b787` | Nominal/idle/rescued/recommended |
| `--orca-warning` | `#d6a344` | Warning status, replanning |
| `--orca-critical` | `#d6544a` | Critical/failed, emergency pulse |
| `--orca-mission-active` | `#4ac9d6` | In-transit/rendezvous, active mission links |
| `--orca-band-leo/meo/geo` | `#3d6ea8` / `#6a52a3` / `#a3527e` | Orbit ring differentiation by band |

Spacing (`--orca-space-1..6`, 4–32px), radius (`sm/md/lg`, 3/6/10px), and typography
(`--orca-text-xs..xl`, `--orca-font-ui`/`--orca-font-mono`) follow the same one-definition
rule.

## 3. Simulation State vs. Visualization State

The distinction brief §81 requires, stated explicitly:

| | Simulation state | Visualization state |
|---|---|---|
| Owned by | The C++ engine, exclusively | The React tree, locally |
| Examples | `SatelliteView.position`, `MissionView.state`, `ParetoPointView.cost` | Hover/selection halo opacity, pulse-ring animation phase, camera position |
| Changes on | A new snapshot arriving | A render frame, a mouse event |
| Ever written back to the engine? | — | **Never** |

Every visualization-state value is computed FROM a simulation-state value already present
in the snapshot (a marker's radius depends on `selected`/`hovered`, both local; its
*position* is `eciToScene(snapshot value)`, never independently animated toward a guessed
future position). No interpolation smooths the actual reported time/position between
snapshots in this phase — polling is frequent enough (500ms default,
`INTEGRATION_ARCHITECTURE.md` §6) that this was not yet needed; brief §31's interpolation
allowance remains available to a future phase without requiring an architecture change.

## 4. Coordinate System

One function, `visualization/coordinates.ts::eciToScene()`, converts every position in the
frontend — no component performs its own axis swap. ECI kilometres (engine convention,
x-y equatorial plane, z polar axis) map to Three.js's Y-up scene space:
`sceneX = eciX / R⊕, sceneY = eciZ / R⊕, sceneZ = eciY / R⊕`, where `R⊕ = 6378.137 km`
(`SCENE_SCALE_KM`) is also Earth's own rendered radius (1 scene unit) — so every other
object's scene position is already relative to Earth's real size, not a distorted "tiny
Earth, huge orbit" scale (brief §15).

## 5. Earth

`visualization/earth/Earth.tsx`: a matte sphere (radius 1), a faint wireframe lat/long
shell (opacity 0.12), and a soft back-facing rim-glow shell (opacity 0.06) for an
atmospheric edge — no postprocessing pipeline, no texture. Deliberately basic per brief §13's
own scope ("do not make Earth photorealistic at the expense of performance"), while
preserving the Earth-centered-composition quality carried forward from Orbital Link.

## 6. Orbit Rings

`visualization/orbit/orbitGeometry.ts::orbitRingPoints()` traces a ring using the exact
position formula `Orbit::positionAtKm()` uses internally, evaluated at 128 parametric phase
angles — a static shape from real orbital elements (altitude/inclination/RAAN), never a
time-dependent propagation and never inventing an element the snapshot did not provide
(brief §29's "do not invent physical trajectories" applies equally here). Color differs by
band (`--orca-band-leo/meo/geo`) — brief §16's "subtle visual differentiation," rendered at
low opacity (0.35) so rings read as structure, not decoration.

**Deferred, documented limitation (Phase 11 brief §32-35)**: unlike the satellite/vehicle
markers (§7 below), orbit rings are NOT instanced — each is drei's own `Line` component, one
draw call per ring. Real, measured impact: at a generated 500-satellite/62-vehicle scenario
(`orca_engine --serve dense:500`), orbit rings account for ~566 of the scene's ~568 draw
calls per frame once markers are instanced (`docs/FINAL_BENCHMARK_REPORT.md`'s real browser
measurement). Left un-instanced because each ring's shape AND orientation genuinely differ
(not just position/scale, unlike the markers), which `THREE.InstancedMesh`'s single shared
geometry does not accommodate without a custom per-instance-geometry shader — judged out of
scope for the time available this phase; triaged as RECOMMENDED, not REQUIRED (the current
scene remains usable at the sizes this project actually ships with).

## 7. Satellite and Vehicle States

`visualization/satellites/EntityMarkerField.tsx` — as of Phase 11 (brief §32-35), one
`THREE.InstancedMesh` per kind (satellites, vehicles) rather than one mesh per object
(Phase 10's `EntityMarker.tsx`, now removed). Per-instance position/scale/color are set via
`InstancedMesh.setMatrixAt()`/`setColorAt()`; click/hover use the real
`ThreeEvent.instanceId` the instanced raycaster already provides, so interaction behavior is
unchanged from Phase 10. **Real, measured improvement** (`docs/FINAL_BENCHMARK_REPORT.md`):
at a generated 500-satellite/62-vehicle scenario, marker draw calls per frame dropped from
562 (one per marker) to 2 (one `InstancedMesh` per kind) — a real browser measurement via
Playwright + instrumented `WebGLRenderingContext.draw*` calls, not a theoretical estimate.
Status color (both satellites and vehicles share one mapping, `STATUS_COLORS` in
`markerColors.ts`):

| Status | Color | Token |
|---|---|---|
| Nominal / Idle / Rescued | green | `--orca-success` |
| Warning | amber | `--orca-warning` |
| Assigned / Transferring / OnStation / Returning | cyan | `--orca-mission-active` |
| Critical / Failed | red | `--orca-critical` |
| Lost / Disabled | grey | `--orca-neutral` |

**Never color-only** (brief §19/§56): every marker's status is also in its hover tooltip as
text, and the selected/critical states each carry a distinct SHAPE cue (below), not only a
color change.

**Vehicles are visually distinct from satellites** (brief §19): an octahedron, not a
sphere — a "directional" silhouette — slightly larger base radius. Not a fully custom
directional-heading indicator (no heading vector exists in the snapshot to point it at
honestly); the distinct geometry alone is the differentiator.

**Selection** (brief §21): a static ring at ~1.8-2.1× the marker's own radius, colored
`--orca-text` (bright, neutral) — distinguishable from the critical pulse ring by both
color and behavior (static vs. animated).

**Emergency pulse** (brief §20): `EmergencyPulseRing`, a single ring scaling from 1× to
~3.2× over a 1.6s loop while fading out — restrained, confined to the marker's own local
space, never a screen-wide flash. Disabled entirely when Settings' "Reduce motion" is on
(a real, functioning preference, not a placebo toggle — brief §49).

**Hover tooltip** (brief §22): real fields only — status, altitude, fuel fraction, and
(vehicles) delta-v remaining, (both) `health.power` labeled "Battery" per brief §22's own
example vocabulary.

## 8. Mission Links (Orbital Link Evolution)

`visualization/trajectories/MissionLink.tsx` renders one line per real `MissionLegView`,
colored/styled by that leg's own `status` field (already engine-computed, never
re-derived):

| Leg status | Color | Style |
|---|---|---|
| `completed` | grey | thin, 35% opacity |
| `current` | amber | solid, thicker, 85% opacity |
| `upcoming` | primary blue | dashed, 85% opacity |

This is Orbital Link's "meaningful connection between objects" concept evolved into a
rescue relationship: a candidate route that was never adopted never appears here at all (no
`MissionLegView` exists for it — the engine only reports the mission's actual, current
route); brief §28's `Candidate`/`Rejected` states apply to the Optimization Lab's Pareto
points (§12 below), not to this line, which only ever shows the ONE route a mission is
actually flying.

**Endpoints are real; the line between them is not a physics claim** (brief §29): exact
mid-leg position is not modelled anywhere in the engine (`DYNAMIC_REPLANNING.md` §13.2), so
a straight connector between two real points is the honest choice — labeled as such in code
comments, never presented as a computed trajectory arc.

**Replanning is visible structurally, not through a separate animation**: when a mission
replans, its NEXT snapshot simply reports a different route — the old leg's line disappears
(no longer in `legs`) and the new one appears, already colored by its real
`current`/`upcoming` status. `MissionIntelligence`'s explanation panel (§10) is where the
REASON for that change is shown, using the engine's own deterministic text.

## 9. Mission Control Layout

`pages/MissionControl/MissionControl.tsx`: the 3D scene fills the dominant left/center
region (brief §70's primary tier); the right column holds, in priority order, transport
controls (compact), Mission Intelligence (§10), and per-object inspection (§11) — the
secondary/tertiary tiers. The bottom event timeline (§13) and top status bar are shell-level
(`AppShell.tsx`), shared by every page, not duplicated per page.

## 10. Mission Intelligence

`components/mission/MissionIntelligence.tsx`. Resolves which mission to describe by
priority: an explicitly selected mission; else a selected satellite/vehicle's associated
mission; else, if exactly one mission is active, that one (so the panel is useful without
requiring a click during a live demo). Every field:

| Field | Source | Honest fallback |
|---|---|---|
| Subject / Severity | `emergency.subjectId`/`severity` | — |
| Responder | `mission.vehicleId` | — |
| ETA | `finalLeg.arrivalTimeSec − simulationTimeSec` | "N/A" if no legs |
| Fuel Cost | `Σ(leg.deltaVKms) / vehicle.deltaVBudgetKms` | "N/A" if vehicle not found |
| Risk | the Pareto front's `recommended` point's `cost.risk` | "N/A" — becomes this after a replan, since the front is not refreshed (§12's own limitation) |
| Success Probability | — | **always** "N/A — not computed by the engine" (brief §24's own explicit example) |
| Why This Plan? | `mission.explanation` (raw `explain()` text) | "No replan has occurred yet" |

Fuel Cost is the one field computed here rather than read verbatim — a sum-then-divide over
real per-leg values already in the snapshot, the "deterministic presentation transformation
of real engine data" brief §61 explicitly permits, not a re-derivation of physics.

## 11. Object Inspector

`components/ObjectInspector.tsx` — per-satellite/vehicle detail, distinct from Mission
Intelligence's per-mission view; falls through to the same "click to select" hint for a
mission-kind selection, since that is Mission Intelligence's concern instead.

## 12. Optimization Lab (Pareto View)

`pages/OptimizationLab/OptimizationLab.tsx` + `charts/ParetoChart.tsx`. X = time, Y = fuel,
point radius = risk (a `recommended` point is fixed-size and highlighted green rather than
risk-scaled, so it never disappears into a low-risk cluster). Dominance/recommendation are
`ParetoPointView.dominated`/`.recommended` booleans — **read directly, never computed in
TypeScript** (brief §41).

**Phase 11 update (brief §50-54)**: the Phase 10 staleness limitation above is fixed. The
front shown is now the mission's CURRENT (most recently captured) planning pass —
`missionParetoHistory` (`cpp/simulation/SimulationState.hpp`) retains every pass rather than
overwriting the original, and the page's "Planning History" section lists all of them. Pass
1 is the real multi-point dispatch-time front; a later pass, from a local replan, is honestly
narrower (a single point: the plan actually adopted, not a full re-derived multi-point front
— `DynamicReplanner`'s internal remaining-route search is not re-run a second time purely for
display).

**Profile controls** (`FASTEST`/`FUEL_SAVING`/`SAFETY_FIRST`/`BALANCED`) are live: clicking
one sends the `rescoreParetoFront` command, which the engine answers by applying
`MissionScorer::selectRecommendedPlan()` directly to the mission's already-captured CURRENT
front — no new search, no change to the front's own points/costs, only which point is marked
`recommended`. There is no `CUSTOM` control; the engine only recognizes the four named
profiles `OptimizationProfile` itself defines.

## 13. Orbital Network

`pages/OrbitalNetwork/`. A presentation-only 2D layout (`networkLayout.ts`: vehicles on an
inner ring, satellites on an outer ring, evenly spaced by index) — explicitly not a claim
about the graph's algorithmic structure (brief §37). Active mission routes are highlighted
by matching real `MissionLegView` endpoints against graph nodes, not recomputed. Edge costs
shown are the graph's build-time reference sample (`GraphEdge::referenceResult()`) — the
page states plainly that this is illustrative, not what any live search decision used
(`cpp/graph/GraphEdge.hpp`'s own file comment).

## 14. Event Timeline

`components/timeline/EventTimeline.tsx`. Six visual categories (`eventCategories.ts`),
mapped from the real `EventKind` wire name — environmental (☄), emergency (⚠), mission (➤),
planning (↻), success (✓), failure (✕). Clicking an event selects its primary related
entity (satellite, else vehicle, else mission) using the event's own real ID fields.

## 15. Presentation Mode

`pages/PresentationMode/`. Full-screen scene, no sidebar/panels/timeline — only the ORCA
brand mark, the emergency-satellite → rescue-vehicle pairing (real IDs), and one status
line derived from the same `useRelevantMission()` hook Mission Control uses (`STANDING BY` /
`MISSION ACTIVE` / `MISSION ACTIVE — REPLANNED` / `RESCUE COMPLETE`) — never a scripted
narration independent of actual mission state.

## 16. Animation Principles

Every animation in this build is either (a) the emergency pulse ring, (b) React's own
mount/hover transitions (CSS, not JS-driven), or (c) `OrbitControls`' inertial camera damping
— nothing else moves on its own. No animation advances using wall-clock time in a way that
could desynchronize from simulation time (brief §30): the pulse ring loops on
`useFrame`'s own clock purely for a decorative cadence, never to represent elapsed
simulation seconds; every *positional* value (marker location, mission link endpoints)
comes directly from the current snapshot, updated only when a new snapshot arrives.

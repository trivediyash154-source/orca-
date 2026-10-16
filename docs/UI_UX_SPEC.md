# ORCA — UI/UX Specification

Version 1.0 · Phase 0 · 2026-09-02

> **Status as of Phase 10: the design system, the nine-screen navigation shell, and Mission
> Control's full visual language are now real — `docs/VISUAL_DESIGN.md` is the accurate,
> authoritative description of what is built (colors, typography, Earth/orbit/entity/link
> visual states, animation principles) and should be read alongside this file, which
> remains the longer-range target design.** Built and real: design tokens
> (`frontend/src/styles/tokens.css`), a top bar with real scenario/time/engine/speed values,
> sidebar navigation across all nine screens (Mission Control, Mission Planner, Orbital
> Network, Optimization Lab, Algorithm Lab, Analytics, Mission History, Scenario, Settings —
> several of the latter are intentionally simple/honest placeholders where the engine does
> not yet expose backing data, per brief §10), Mission Control's full scene (Earth with
> atmosphere/rim-glow, band-colored orbit rings, status-colored entity markers with
> selection halos and an emergency pulse ring, real mission links colored by leg status),
> Mission Intelligence (real ETA/fuel/risk, honest N/A for success probability, the
> engine's own deterministic replan explanation), the event timeline, a real Orbital
> Network graph view, and a real Pareto-front Optimization Lab view. Presentation Mode
> works. §1's engineering-instrumentation feel and restrained-glow principle are now
> actually implemented, not only stated as intent — see `VISUAL_DESIGN.md` for exactly how.

---

## 1. Design Intent

ORCA should read as **engineering software**: an aerospace mission display crossed with a
scientific visualisation tool.

| It should feel like | It must not feel like |
|---|---|
| Mission control telemetry | A generic admin dashboard |
| Scientific visualisation | A game HUD |
| High-end engineering instrumentation | A sci-fi movie prop |
| A tool built by people who understand the data | A neon-drenched "futuristic" theme |

**Restraint is the governing rule.** Glow, animation and saturation are used to encode
*meaning* — never decoration. If a visual effect does not carry information, it is removed.

### 1.1 Visual hierarchy

Fixed and non-negotiable. Every layout decision defers to this order:

```
1. Earth / orbital scene       ← the dominant anchor
2. Active emergency            ← what demands attention
3. Mission trajectory          ← the response
4. Optimization result         ← why this response
5. Telemetry                   ← supporting detail
6. Analytics                   ← historical context
```

### 1.2 Originality

Inspiration is drawn from the *conventions* of aerospace displays, engineering dashboards and
scientific visualisation. **No proprietary interface is reproduced.** No real agency's
branding, layout, colour system or iconography is imitated.

### 1.3 Design lineage — Orbital Link

ORCA's orbital scene should read as an **evolution of the Orbital Link project's visual
language**, not as an unrelated dashboard with a globe in it.

> **Scope note.** Orbital Link is not present in this repository and was not available to
> inspect. What follows is the set of *design qualities* to carry forward, recorded as a
> quality benchmark. No code, asset or layout is copied — this is a target to match, not a
> source to reproduce.

The qualities to inherit:

| Quality | What it means for ORCA |
|---|---|
| Earth-centred spatial composition | Earth is the anchor and the largest object on screen; panels orbit it rather than compete with it |
| Elegant orbital paths | Rings are thin, smooth, and continuous — geometry, not decoration |
| Meaningful connections between objects | A drawn line always means a real relationship, never a flourish |
| Clear visual hierarchy | The eye lands on Earth, then the emergency, then the response |
| Restrained glow | Glow marks *state*, never surface. Used on active trajectories and selection only |
| Smooth movement | Motion is continuous and eased; nothing snaps or jitters |
| Technically understandable geometry | A viewer can infer altitude, inclination and phase from what is drawn |
| Minimal clutter | Labels on demand; markers small; debris low-opacity |
| Visually obvious relationships | Which vehicle is going to which casualty is readable in under a second |

**The conceptual progression** ORCA extends:

```
Orbital Link          static connections between orbital objects
      ↓
orbital connections   which objects can reach which — the feasibility graph
      ↓
rescue transfer links a specific vehicle-to-casualty route
      ↓
dynamic trajectories  that route being flown, and re-drawn when it changes
      ↓
optimization alts     the routes ORCA considered and rejected
```

Each step adds meaning to the same visual primitive — a line between two orbital objects.
That is the through-line: ORCA does not invent a new visual language, it gives the existing
one **more to say**. A candidate route, a recommended route, and an active transfer are the
same primitive at three levels of commitment (§4.2), which is why their visual states differ
by weight and opacity rather than by shape.

**What this requires of the engine**, and why it appears in a UI document: the frontend can
only draw these relationships if the engine ships them. That means edge data with costs
(`graphData`, `API_CONTRACT.md` §4.6), sampled trajectory polylines (`DATA_MODEL.md` §5), and
the rejected alternatives retained on each mission. The visual language and the data contract
were designed together; neither is achievable alone.

---

## 2. Design System

### 2.1 Colour

```
Background — deep space
  --bg-void        #05070d    scene background, behind everything
  --bg-base        #0a0e18    application background
  --bg-surface     #111827    panels
  --bg-elevated    #1a2333    cards, popovers
  --border         #253044    1px dividers
  --border-strong  #35435c    emphasised dividers

Text
  --text-primary   #e6ebf5
  --text-secondary #9aa8bf
  --text-tertiary  #5f6d84
  --text-mono      #c8d4e8    numeric readouts

Semantic — meaning only, never decoration
  --accent         #38bdf8    primary/selection (cyan-blue)
  --accent-dim     #0e7490
  --nominal        #4ade80    healthy / success
  --warning        #fbbf24    degraded
  --critical       #f87171    failed / emergency
  --rescue         #a78bfa    rescue vehicle (distinct from all status colours)
  --debris         #64748b    low-emphasis hazard
  --inactive       #374151    disabled / rejected
```

**Accessibility.** All text pairs meet WCAG AA (≥4.5:1) against their surface.
`--text-primary` on `--bg-surface` is ≈13:1. Status is **never encoded by colour alone** —
every status carries a shape, a label, or both (§4.1), so the display remains readable for
red/green colour-blind viewers, who would otherwise struggle with `--nominal` vs `--critical`.

### 2.2 Typography

```
UI          Inter (or system sans)        400 / 500 / 600
Numeric     JetBrains Mono                400 / 500      ← tabular figures
```

**All numeric readouts are monospaced with tabular figures.** Telemetry that shifts
horizontally as digits change is the single fastest way to make a display feel amateurish.

```
Display   28px/600   screen titles
H1        20px/600   panel headers
H2        16px/600   section headers
Body      14px/400
Small     12px/400   labels
Micro     11px/500   uppercase, 0.06em tracking — axis labels, chips
Mono-L    24px/500   primary metrics
Mono-M    16px/400   telemetry
Mono-S    12px/400   dense tables
```

### 2.3 Space, radius, motion

8px grid: `4 · 8 · 12 · 16 · 24 · 32 · 48 · 64`.
Radius: `2px` inputs, `4px` panels, `6px` cards. No pills, no heavy rounding.

```
instant   100ms   hover, focus
quick     180ms   panel open, tooltip
standard  280ms   screen transition, trajectory redraw
slow      600ms   camera moves
pulse    1800ms   emergency marker, loops
easing   cubic-bezier(0.4, 0.0, 0.2, 1)
```

**`prefers-reduced-motion` is honoured**: pulses become static rings, camera moves become
cuts, trajectory animations become instant redraws. No information is conveyed by motion
alone.

---

## 3. Application Shell

```
┌──────────────────────────────────────────────────────────────────────────────┐
│ ORCA │ demo-alpha ▾ │ T+ 00:13:43 │ ● RUNNING │ ◀◀ ▮▮ ▶▶ 4× │ ⚠ 2 ACTIVE     │  56px
├────┬─────────────────────────────────────────────────────────────────────────┤
│    │                                                                         │
│ N  │                          SCREEN CONTENT                                 │
│ A  │                                                                         │
│ V  │                                                                         │
│    │                                                                         │
│56px│                                                                         │
├────┴─────────────────────────────────────────────────────────────────────────┤
│ EVENT TIMELINE                                                       ▲ 120px  │
└──────────────────────────────────────────────────────────────────────────────┘
```

**Top bar** (always visible): wordmark · scenario selector · `T+` mission-elapsed clock
(monospace) · run state · transport controls (step back / pause / play / step / speed) ·
active-emergency counter, which turns `--critical` and pulses when > 0.

**Left rail** — icon + tooltip, keyboard `1`–`9`:

| | Screen | Key |
|---|---|---|
| ◎ | Mission Control | `1` |
| ⌖ | Mission Planner | `2` |
| ⚖ | Optimization Lab | `3` |
| ⛓ | Orbital Network | `4` |
| ⚙ | Algorithm Lab | `5` |
| ▤ | Scenario Builder | `6` |
| ▦ | Analytics | `7` |
| ⏱ | Mission History | `8` |
| ⚒ | Settings | `9` |

**Bottom timeline** collapsible to 32px. Global shortcuts: `Space` pause/resume,
`←`/`→` step, `+`/`−` speed, `Esc` clear selection, `?` shortcut overlay.

---

## 4. Visual Language

### 4.1 Object markers

Colour **and** shape encode status, so neither alone is load-bearing.

| Object | Shape | Colour | Size | Notes |
|---|---|---|---|---|
| Satellite — nominal | circle | `--accent` 60% | 4px | small, cool, quiet |
| Satellite — warning | circle + ring | `--warning` | 5px | |
| Satellite — critical | circle + ring | `--critical` | 6px | |
| Satellite — failed | filled diamond | `--critical` | 7px | **pulses** 1.8 s |
| Satellite — rescued | circle + check | `--nominal` | 5px | |
| Satellite — lost | hollow circle | `--inactive` | 4px | 40% opacity |
| Rescue vehicle | **triangle** | `--rescue` | 7px | always distinct |
| RV transferring | triangle + trail | `--rescue` | 7px | short motion trail |
| Debris | dot | `--debris` | 1–2px | 25% opacity, no labels |
| Debris (detected hazard) | dot + faint sphere | `--warning` | 2px | hazard radius shown |
| Selected | any + **halo ring** | `--accent` | +4px | |

**Markers stay small.** They are screen-space sized (4–7px regardless of camera distance) so
they remain visible when zoomed out, but they never become icons. The brief's instruction not
to make them giant is a hard constraint: at 500 objects, oversized markers turn the scene into
noise.

Labels appear only for: the selected object, active emergencies, and rescue vehicles on
mission. Everything else is labelled on hover.

### 4.2 Trajectory states

| State | Stroke | Width | Opacity | Animation |
|---|---|---|---|---|
| `CANDIDATE` | `--text-tertiary` dashed 4/4 | 1px | 35% | none |
| `RECOMMENDED` | `--accent` solid | 2px | 100% | draw-on 280ms |
| `ACTIVE` | `--accent` solid + glow | 2.5px | 100% | flowing dash along path |
| `COMPLETED` | `--nominal` solid | 1.5px | 60% | none |
| `REJECTED` | `--critical` dashed 2/6 | 1px | 25% | fade out 280ms |

The flowing dash on `ACTIVE` is the one animation that carries information — direction of
travel. Under `prefers-reduced-motion` it becomes a static arrowhead.

---

## 5. Screen 1 — Mission Control *(primary)*

```
┌──────────────────────────────────────────────────────────────────────────────┐
│ ORCA │ demo-alpha │ T+ 00:13:43 │ ● RUNNING │ ◀◀ ▮▮ ▶▶ 4× │ ⚠ 1 ACTIVE       │
├────┬────────────────────────────────────────────────┬────────────────────────┤
│    │                                                │ MISSION INTELLIGENCE   │
│ ◎  │                    ╭─────╮                     │ ┌────────────────────┐ │
│ ⌖  │              ╭─────┤     ├─────╮               │ │ ⚠ CRITICAL         │ │
│ ⚖  │           ╭──┤     │  ●  │     ├──╮            │ │                    │ │
│ ⛓  │          ╱   ╰─────┤     ├─────╯   ╲           │ │ SAT-017            │ │
│ ⚙  │         │      ▲   ╰─────╯    ◆     │          │ │ Meridian-3         │ │
│ ▤  │         │    RV-02          SAT-017 │          │ │ Propulsion failure │ │
│ ▦  │          ╲        ·  ·  ·          ╱           │ │                    │ │
│ ⏱  │           ╰──╮               ╭────╯            │ │ SURVIVAL WINDOW    │ │
│ ⚒  │              ╰───────────────╯                 │ │ 25:08              │ │
│    │                                                │ │ ▓▓▓▓▓▓▓▓▓░░░ 78%   │ │
│    │  LEO ─── MEO ─── GEO      [◱ reset view]       │ ├────────────────────┤ │
│    │                                                │ │ RECOMMENDED        │ │
│    │                                                │ │ RV-02 → SAT-017    │ │
│    │                                                │ │                    │ │
│    │                                                │ │ ETA        11:48   │ │
│    │                                                │ │ FUEL       13.6%   │ │
│    │                                                │ │ RISK        7.2%   │ │
│    │                                                │ │ SUCCESS    94.1%   │ │
│    │                                                │ │                    │ │
│    │                                                │ │ [ DISPATCH ]       │ │
│    │                                                │ │ [ view front → ]   │ │
├────┴────────────────────────────────────────────────┴────────────────────────┤
│ ⚠ 06:52 Failure SAT-017  ⚙ 06:52 Graph V=28 E=173  ◆ 06:53 Plan RV-02  …     │
└──────────────────────────────────────────────────────────────────────────────┘
```

### 5.1 Orbital scene (centre, ~65% width)

Earth rendered with realistic proportions — `R⊕ = 6378 km` to scale against orbital radii, so
LEO sits visibly close to the surface and GEO far out. That relationship is information, not
styling.

- **Surface:** dark, low-contrast texture. Subtle city lights on the night side.
- **Atmosphere:** thin rim glow, 2–3px, `--accent` at very low opacity. One shell only.
- **Terminator:** soft day/night boundary.
- **Orbit rings:** 1px, 20% opacity, `--text-tertiary`; labelled `LEO`/`MEO`/`GEO` once each.
- **Inclined paths:** shown only for objects that are selected or on mission — drawing all
  inclinations at once is unreadable.
- **Background:** `--bg-void` with a faint static starfield. No nebulae, no lens flares.

**Camera:** orbit-drag, scroll-zoom, double-click to focus an object, `R` to reset. Zoom
clamped between "Earth fills 30% of the viewport" and "GEO ring fully visible".

**Interaction:** hover shows an ID tooltip; click selects and fills the right panel;
click-empty deselects.

### 5.2 Mission Intelligence (right, ~35%, min 340px)

Three stacked cards, driven **entirely** by engine snapshot data.

**Emergency card** — severity chip, satellite ID and name, failure kind, survival-window
countdown (`remainingWindowSec` from the engine, *not* computed client-side), and a depletion
bar that shifts `--nominal` → `--warning` → `--critical` as the window closes.

**Planning card** — three states:

```
┌────────────────────┐   ┌────────────────────┐   ┌────────────────────┐
│ ORCA STATUS        │   │ RECOMMENDED        │   │ NO FEASIBLE PLAN   │
│                    │   │ RV-02 → SAT-017    │   │                    │
│ ◐ Planning…        │   │ ETA        11:48   │   │ All routes exceed  │
│                    │   │ FUEL       13.6%   │   │ RV-02's Δv budget  │
│ Building graph     │   │ RISK        7.2%   │   │ by ≥ 0.75 km/s     │
│ V=28 E=173         │   │ SUCCESS    94.1%   │   │                    │
│ Labels 412→8       │   │ [ DISPATCH ]       │   │ 412 edges rejected │
└────────────────────┘   └────────────────────┘   │ on fuel            │
                                                   └────────────────────┘
```

The planning state deliberately exposes `V`, `E` and the label counts. This is a DSA project;
showing the algorithm working is a feature, not debug output.

**Telemetry card** — for the selected object: position, altitude, inclination, subsystem
health bars, Δv remaining.

### 5.3 Event timeline (bottom)

Horizontally scrolling, newest right, auto-scrolls unless the user has scrolled away.
Icon + colour by kind, `mm:ss` timestamp, one-line message. Click to select the subject and
seek the camera. Filter chips: `All · Emergencies · Missions · Replans · System`.

### 5.4 States

| State | Display |
|---|---|
| No scenario | Centred prompt: *"Load a scenario to begin"* + scenario picker |
| Stopped | Scene rendered static at `t=0`; panel shows scenario summary |
| No emergencies | Panel: *"All systems nominal — 24 satellites tracked"* |
| Engine down | Scene dims to 40%, red banner: *"Engine stopped unexpectedly"* + stderr tail + `Restart` |
| Reconnecting | Amber banner *"Reconnecting…"*; last-known state dimmed, controls disabled |

---

## 6. Screen 2 — Mission Planner

Detail view for one emergency: target, candidate vehicles, route table, objective breakdown,
feasibility.

```
┌─────────────────────────┬────────────────────────────────────────────────────┐
│ TARGET                  │ CANDIDATE ROUTES                    8 non-dominated│
│ SAT-017 · CRITICAL      │ ┌────────────────────────────────────────────────┐ │
│ Window 25:08            │ │ ID     RV     TIME    FUEL   RISK   DIST   ⬤   │ │
│                         │ │ P-0012 RV-02  09:00  24.2%  18.1%  1502  ○     │ │
│ RESPONDERS              │ │ P-0007 RV-02  11:48  13.6%   7.2%  1843  ●     │ │
│ ▸ RV-02  Δv 2.16 km/s   │ │ P-0019 RV-03  17:05   9.8%   4.1%  2210  ○     │ │
│ ▸ RV-03  Δv 1.74 km/s   │ │ …                                              │ │
│ ✕ RV-01  out of range   │ └────────────────────────────────────────────────┘ │
│                         │ OBJECTIVE BREAKDOWN — P-0007                       │
│ GRAPH                   │ time  ▓▓▓▓▓░░░░░░░░░  0.347                        │
│ V 28  E 173             │ fuel  ▓▓▓░░░░░░░░░░░  0.264                        │
│ rejected 583            │ risk  ▓▓░░░░░░░░░░░░  0.221                        │
│  fuel 511               │ dist  ▓▓▓▓▓▓░░░░░░░░  0.481                        │
│  deadline 72            │                                                    │
│  risk 0                 │ WHY THIS ROUTE                                     │
│                         │ Balances a 31% time penalty against 44% lower fuel │
│                         │ and 60% lower risk than the fastest option.        │
└─────────────────────────┴────────────────────────────────────────────────────┘
```

Selecting a row previews that route on the Mission Control scene as `CANDIDATE`. The
recommended row is marked `●`. The rejection breakdown by reason turns "no feasible path" into
something an operator can act on.

---

## 7. Screen 3 — Optimization Lab

**The screen that makes the two-stage design tangible.**

```
┌────────────────────────┬─────────────────────────────────────────────────────┐
│ PREFERENCE             │ PARETO FRONT                                        │
│                        │  fuel                                               │
│ ○ Fastest              │  0.25│    ●P-0012                                   │
│ ● Balanced             │      │  ○   ○                                       │
│ ○ Fuel Saving          │  0.20│      ○    ○                                  │
│ ○ Safety First         │      │   ○                                          │
│ ○ Custom               │  0.15│         ◉P-0007  ← recommended               │
│                        │      │      ○      ○                                │
│ WEIGHTS                │  0.10│                 ●P-0019                      │
│ Time      ▬▬▬▬●▬▬  .30 │      └──────────────────────────────── time         │
│ Fuel      ▬▬▬▬●▬▬  .30 │       09:00   11:00   13:00   15:00   17:00         │
│ Risk      ▬▬▬▬●▬▬  .30 │                                                     │
│ Distance  ▬●▬▬▬▬▬  .10 │  ● front   ○ dominated   ◉ recommended              │
│                        │  point size = risk                                  │
│ [ reset ]              │                                                     │
│                        │ ┌─────────────────────────────────────────────────┐ │
│ SEARCH                 │ │ Front computed once. Moving a slider re-selects  │ │
│ labels generated  412  │ │ from these 8 points — it does not re-search.     │ │
│ pruned            371  │ └─────────────────────────────────────────────────┘ │
│ non-dominated       8  │                                                     │
│ time           11.4ms  │                                                     │
└────────────────────────┴─────────────────────────────────────────────────────┘
```

**Chart:** X = rescue time, Y = fuel cost, **point radius = risk** (3–10px), colour
distinguishes front / dominated / recommended. Axis-swap control allows any objective pair.

**Behaviour that must be exactly right:** dragging a slider moves *only* the recommendation
marker. The point cloud is **static**. A visible re-render of the front on every slider tick
would communicate the opposite of the architecture. Requests carry `reqId`; stale responses
are discarded (`API_CONTRACT.md` §7).

Debounce 80 ms. Hovering a point shows its full objective vector; clicking it pins it as a
manual override, which switches the profile to `CUSTOM`.

---

## 8. Screen 4 — Orbital Network

Makes the physical↔graph correspondence explicit.

Split view — **left:** the 3D orbital scene; **right:** the same objects as a 2D node-link
graph. **Hovering a node highlights the same object in both views.** That linkage is the
entire point of the screen: it shows the evaluator that the graph is a real abstraction of the
simulated environment, not a decorative diagram.

- Layout coordinates come from the engine (`API_CONTRACT.md` §4.6) — the frontend runs no
  layout algorithm.
- Node colour by kind: vehicle source `--rescue`, casualty sink `--critical`, staging
  `--accent`.
- Edge opacity by cost (cheaper = brighter). Recommended-route edges at full `--accent`.
- Toggles: show rejected edges (very faint, `--inactive`), show weights, filter by objective.
- Stats bar: `V`, `E`, rejection counts, build time.

---

## 9. Screen 5 — Algorithm Lab

**The screen the DSA viva is built around.**

```
┌────────────────────────┬─────────────────────────────────────────────────────┐
│ ALGORITHM              │ SEARCH STATE                        step 37 / 112   │
│ ○ Dijkstra             │                                                     │
│ ● A*                   │        ○───────○───────◍                            │
│ ○ Pareto               │       ╱ ╲     ╱ ╲     ╱                             │
│                        │      ◉───────○───────○         ◍ current            │
│ ▮◀ ◀ ▶ ▶▮  ▶▶ auto     │       ╲ ╱     ╲ ╱     ╲        ◉ source             │
│                        │        ○───────◍───────◎       ◎ goal               │
│ METRICS                │                                 ● visited           │
│ nodes expanded    37   │                                 ○ unvisited         │
│ edges relaxed    112   │                                                     │
│ pq pushes        112   │ NODE TABLE                                          │
│ pq pops           98   │  node     g        h        f       state           │
│ wall time      1.2ms   │  N-0003    0.0   486.2   486.2   visited            │
│                        │  N-0011  302.1   198.4   500.5   CURRENT            │
│ COMPARISON             │  N-0019  411.7   240.0   651.7   frontier           │
│ Dijkstra  86 expanded  │  N-0027  380.2   190.1   570.3   frontier           │
│ A*        37 expanded  │                                                     │
│ ▸ 57% fewer            │                                                     │
└────────────────────────┴─────────────────────────────────────────────────────┘
```

Traces come from the engine (`traceAlgorithm`, `API_CONTRACT.md` §4.7). **The frontend replays
a trace; it never runs a search.**

**Pareto mode** replaces the node table with a label table:

```
LABELS AT N-0027                      generated 412 · pruned 371 · retained 41
  label    time    fuel    risk    dist    status
  L-0114   302.1   0.061   0.028    812    non-dominated
  L-0187   340.8   0.052   0.031    798    non-dominated
  L-0203   355.2   0.078   0.044    841    PRUNED — dominated by L-0114
```

Pruned labels flash `--critical` for 400 ms as they are discarded, then fade to `--inactive`.
This makes dominance *visible* — and it is the single most useful thing in the product for
explaining the algorithm to an examiner.

---

## 10. Screens 6–9

### Scenario Builder
Form + live 3D preview. Satellite/vehicle/debris counts, orbital band distribution,
inclination spread, Δv budgets, failure schedule, **seed** (with a dice button and an explicit
"same seed reproduces this scenario exactly" note). Save to `scenarios/`. Validation is inline
and blocking: an invalid scenario cannot be saved.

### Analytics
Metric tiles — total emergencies, successful rescues, failed rescues, mean rescue time, mean
fuel, mean risk, replan count, mean algorithm runtime, nodes explored, Pareto candidates.
Below: outcome over time, fuel distribution, risk distribution, strategy comparison bars.
`Export CSV` / `Export JSON`.

**Empty state:** *"No completed missions yet"* — never zeros, which read as real measurements.

### Mission History
Table of completed missions: ID, vehicle, casualty, outcome, duration, fuel, risk, replans.
Click to expand a full timeline with the route taken, alternatives rejected, and any replan
decision points. Filterable, sortable, exportable.

### Settings
Simulation (`Δt`, snapshot rate, default speed) · Optimization (default profile, `ε`, label
cap, switch margin) · Visualisation (marker scale, show debris, show rejected edges, reduced
motion) · Engine (path, restart policy, log level) · About (versions, `schemaVersion`).

Every setting that changes engine behaviour is sent to the engine; none is applied
frontend-side.

---

## 11. Cross-Cutting States

### Empty
Every list, chart and panel has a designed empty state with an explanatory sentence and,
where useful, an action. **Never an empty box, never a zero presented as a measurement.**

### Loading
- < 200 ms: nothing (avoids flicker).
- 200 ms–2 s: inline spinner with the operation named (*"Building graph…"*).
- \> 2 s: progress detail from engine events (*"Labels 2841 / 5000"*).

Skeleton screens on first scenario load only.

### Error
Inline and specific, at the point of failure. Every message names the thing that failed and,
where possible, the fix. Full mapping in `ARCHITECTURE.md` §11.

```
┌──────────────────────────────────────────────────────────┐
│ ⚠  Engine stopped unexpectedly                           │
│                                                          │
│ Exit code 139 (segmentation fault)                       │
│ [ERROR] t=412.0 assertion failed:                        │
│         adjacency_.size() == nodes_.size()               │
│                                                          │
│ [ Restart engine ]   [ Copy log ]                        │
└──────────────────────────────────────────────────────────┘
```

Showing the assertion text is deliberate — the primary user is the developer, and hiding the
cause helps nobody.

### Tooltips
150 ms delay. Every abbreviation, every unit, every metric. Δv, Pareto dominance, non-dominated,
label, admissible heuristic, synodic period, and `fuel %` (*"percentage of this vehicle's own
Δv budget — not comparable across vehicles"*, per `SIMULATION_MODEL.md` §6) all carry
explanatory tooltips.

---

## 12. Responsive Behaviour

Desktop-class tool; not a mobile product.

| Width | Behaviour |
|---|---|
| ≥ 1600px | Full layout as specified |
| 1280–1599 | Right panel 320px; timeline 96px |
| 1024–1279 | Right panel becomes an overlay drawer; rail icons only |
| < 1024 | Notice: *"ORCA requires a display at least 1024px wide"* |

The 3D canvas resizes with device-pixel-ratio clamped at 2 to bound fragment cost.

---

## 13. Presentation Mode

Full-screen, chrome hidden, a caption strip at the bottom, `→` to advance. Runs the **real
demo scenario at a fixed seed** — it is a camera-and-pacing script over a live simulation,
never a recording.

```
1  "24 satellites across three orbital bands"        → wide orbital view
2  "T+06:52 — SAT-017 propulsion failure"            → zoom to casualty, pulse
3  "ORCA builds the mission graph: V=28, E=173"      → cut to Network
4  "Label-setting: 412 generated, 371 pruned"        → cut to Algorithm Lab
5  "8 non-dominated plans"                            → cut to Pareto front
6  "BALANCED selects RV-02 — 11:48, 13.6%, 7.2%"     → highlight point
7  "Dispatch"                                         → back to scene, trajectory draws
8  "T+09:58 — debris detected on the active route"   → debris flash, risk climbs
9  "Replanning: risk 0.31 → 0.09 for +42s"           → trajectory switches
10 "Rendezvous. Rescue complete."                     → success state
11 "1 rescued · 0 lost · 1 replan · 11.4ms planning" → Analytics
```

Timing configurable in `configs/presentation.json`. Any step can be paused and inspected —
because it is a live run, the underlying screens remain fully interactive.

---

## 14. Component Inventory

```
frontend/src/components/
├── shell/       AppShell · TopBar · NavRail · TimelineBar · StatusBadge
├── panels/      Panel · Card · MetricTile · StatChip · EmptyState · ErrorPanel
├── controls/    Button · Slider · Toggle · Select · SeedInput · TransportControls
├── data/        DataTable · ObjectiveBar · ProgressBar · CountdownTimer
└── feedback/    Tooltip · Toast · Spinner · Skeleton · ConnectionBanner

frontend/src/visualization/
├── scene/       SceneRoot · CameraRig · Lighting · Starfield · Postprocessing
├── earth/       EarthMesh · Atmosphere · Terminator
├── orbit/       OrbitRing · InclinedPath · BandLabels
├── satellites/  SatelliteMarkers (instanced) · VehicleMarkers · DebrisField · SelectionHalo
├── trajectories/TrajectoryLine · TrajectoryAnimator · CandidateOverlay
└── graph/       NetworkGraph · GraphNode · GraphEdge · LayoutRenderer

frontend/src/charts/
    ParetoScatter · MetricBar · DistributionChart · TimeSeriesChart · StrategyComparison
```

**Rule.** Components in `components/` and `charts/` are presentational — they receive engine
data as props and render it. No component computes a mission value. Formatting (seconds →
`mm:ss`, fraction → `13.6%`) is presentation and is allowed; derivation is not
(`ARCHITECTURE.md` §6.1).

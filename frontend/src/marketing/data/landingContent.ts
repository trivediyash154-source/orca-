/**
 * Every word and every number on the entry surface, in one place.
 *
 * Two rules govern this file, and they are the reason it exists rather than the copy living
 * inline in each section:
 *
 * 1. **No number here is invented.** Each figure is transcribed from this repository's own
 *    documents and carries its `source` path so a reader can check it. Figures are quoted at
 *    the precision the source reports them -- an unflattering 0.01334 ms stays 0.01334 ms.
 * 2. **No caveat is dropped.** Where a measurement has a limitation that changes how it should
 *    be read (A* being marginally slower at size 25; the assignment result resting on n=15),
 *    the caveat is a required field on the record, not an optional footnote. A section cannot
 *    render the finding without having the caveat in hand.
 *
 * The landing page performs no engine calls. Nothing below is live telemetry, and the copy
 * says so where a reader might otherwise assume it.
 */

/* --- Types ---------------------------------------------------------------- */

export interface TelemetryChip {
  readonly label: string;
  readonly value: string;
  readonly unit?: string;
  readonly note: string;
}

export interface HeroContent {
  readonly eyebrow: string;
  /** Split so the second clause can carry its own treatment. Rendering them as one string
   * would force the section to slice the headline, which breaks translation and screen-reader
   * phrasing. */
  readonly headlineLead: string;
  readonly headlineEmphasis: string;
  readonly lede: string;
  readonly primaryCta: string;
  readonly secondaryCta: string;
  readonly chips: readonly TelemetryChip[];
}

export interface ProblemConstraint {
  readonly index: string;
  readonly tag: string;
  readonly title: string;
  readonly body: string;
}

export interface PipelineStage {
  readonly id: string;
  readonly index: string;
  readonly tag: string;
  readonly title: string;
  readonly body: string;
  readonly inputs: readonly [string, string, string];
  readonly output: string;
}

export interface Objective {
  readonly key: string;
  readonly label: string;
  /** Not a union. Every term the engine carries is minimised, and `ObjectiveVector.hpp` states
   * why that is structural rather than stylistic: one "higher is better" term would put
   * per-objective knowledge inside the dominance predicate and make every proof about it
   * conditional. Widening this type would be a claim about the engine, not a refactor. */
  readonly direction: 'minimise';
  readonly unit: string;
  readonly note: string;
}

export interface Capability {
  readonly key: string;
  readonly title: string;
  readonly body: string;
  readonly tags: readonly string[];
  /** Present only where the body states a measured figure. A claim about design carries no
   * source because there is nothing to check; a claim about a number carries one because
   * there is. */
  readonly source?: string;
}

export interface ResearchTable {
  readonly caption: string;
  readonly columns: readonly string[];
  readonly rows: readonly (readonly string[])[];
}

export interface ResearchResult {
  readonly id: string;
  readonly question: string;
  readonly finding: string;
  /** Never optional. A result whose limitation is not stated has not been reported. */
  readonly caveat: string;
  readonly source: string;
  readonly table: ResearchTable;
}

export interface ArchitectureLevel {
  readonly level: string;
  readonly name: string;
  readonly stack: string;
  /** The one-word role the README itself gives each level. */
  readonly role: string;
  readonly body: string;
}

export interface AlgorithmEntry {
  readonly name: string;
  readonly role: string;
}

/* --- Hero ----------------------------------------------------------------- */

export const HERO: HeroContent = {
  eyebrow: 'Autonomous orbital rescue mission planning',
  headlineLead: 'Plan a rescue against objectives',
  headlineEmphasis: 'that genuinely conflict',
  lede:
    'ORCA searches the trade space between propellant, transfer time and collision risk and ' +
    'returns the Pareto-optimal set — not a single scalar a planner is asked to trust. The ' +
    'search runs in a standalone C++20 engine; this interface exists to display what the ' +
    'engine decided, and why.',
  primaryCta: 'Enter the workspace',
  secondaryCta: 'Read the evaluation',
  chips: [
    {
      label: 'Synodic period',
      value: '25.4',
      unit: 'h',
      note: 'Between a 500 km and an 800 km orbit — the recurrence of the fuel-optimal phase.',
    },
    {
      label: 'Pareto scaling',
      value: '45.9',
      unit: '×',
      note: 'Label-setting search cost, 25 to 100 objects. docs/RESEARCH_EVALUATION.md §5.2',
    },
    {
      label: 'Draw calls',
      value: '−49.6',
      unit: '%',
      note: '1,128 to 568 per frame after instancing. docs/FINAL_BENCHMARK_REPORT.md §6',
    },
    {
      label: 'Engine tests',
      value: '675',
      note: 'C++ test cases, plus 29 backend and 71 frontend. README',
    },
  ],
};

/* --- The problem ----------------------------------------------------------
   Three constraints, stated as physics rather than as pain points. The middle one carries the
   synodic-period figure because it is the single fact that proves the conflict is real and not
   a framing device invented to justify a multi-objective solver. */

export const PROBLEM_CONSTRAINTS: readonly ProblemConstraint[] = [
  {
    index: '01',
    tag: 'Target state',
    title: 'The target moves, and its state is only partly known',
    body:
      'A disabled vehicle keeps propagating along its orbit while the plan is being computed, ' +
      'and the elements describing it carry uncertainty that grows with time since the last ' +
      'fix. A planner is therefore not routing to a point but to a region that is drifting ' +
      'while the route is chosen.',
  },
  {
    index: '02',
    tag: 'Margins',
    title: 'Propellant, transfer time and window margins are all finite',
    body:
      'The synodic period between a 500 km and an 800 km orbit is 25.4 hours, so the ' +
      'fuel-optimal departure phase recurs roughly once a day. Survival windows run to tens ' +
      'of minutes. Waiting for the cheapest departure and arriving in time are, in the ' +
      'general case, incompatible requests — the conflict is in the orbital mechanics, not in ' +
      'the problem statement.',
  },
  {
    index: '03',
    tag: 'Coupling',
    title: 'Safety cannot be optimised in isolation',
    body:
      'A trajectory that avoids a debris field costs propellant and time; a faster transfer ' +
      'raises closing velocity and narrows the abort envelope; holding reserve for the return ' +
      'leg constrains the outbound burn. Improving any one term moves the others, which is why ' +
      'the output is a frontier of admissible compromises rather than a single answer.',
  },
];

/* --- Pipeline -------------------------------------------------------------
   Five stages. The fourth is explicitly a human step: the engine produces the nondominated
   set, and choosing a point on it is an engineering decision that must stay visible rather
   than being hidden inside a default weighting. */

export const PIPELINE_STAGES: readonly PipelineStage[] = [
  {
    id: 'define',
    index: '01',
    tag: 'Scenario',
    title: 'Define the scenario',
    body:
      'Fix what is actually known: the orbital elements of every object in play, the rescue ' +
      "vehicle's propellant state, and the declared emergency with its survival deadline.",
    inputs: ['Orbital elements', 'Vehicle propellant state', 'Emergency declaration'],
    output: 'A fully specified initial state',
  },
  {
    id: 'constrain',
    index: '02',
    tag: 'Envelope',
    title: 'Constrain the feasible envelope',
    body:
      'Discard what cannot be flown before searching what can. Transfers beyond the delta-v ' +
      'budget, arrivals after the deadline and paths through occupied debris cells never enter ' +
      'the graph, so the search never spends time proving them unusable.',
    inputs: ['Delta-v budget', 'Debris field occupancy', 'Survival window'],
    output: 'A feasible weighted transfer graph',
  },
  {
    id: 'search',
    index: '03',
    tag: 'Search',
    title: 'Search and evaluate',
    body:
      'Multi-objective label-setting search (Martins’) expands labels under Pareto ' +
      'dominance pruning, so a path is discarded only when another path is at least as good on ' +
      'every objective. What survives is the nondominated set, not the winner of a weighting.',
    inputs: ['Transfer graph', 'Objective vector', 'Dominance rule'],
    output: 'The nondominated (Pareto) set',
  },
  {
    id: 'compare',
    index: '04',
    tag: 'Judgement',
    title: 'Compare and refine',
    body:
      'Every point on the frontier is optimal; they differ in what they spend. Selecting one is ' +
      'an engineering decision, so the trade is shown explicitly — this much propellant buys ' +
      'this much time — and the choice is recorded with the plan rather than absorbed into a ' +
      'default weight.',
    inputs: ['Pareto set', 'Mission priority', 'Operator constraint'],
    output: 'A selected plan with its trade recorded',
  },
  {
    id: 'replan',
    index: '05',
    tag: 'Closed loop',
    title: 'Update and replan',
    body:
      'The state that was planned against is already out of date once a burn executes. Measured ' +
      'position, achieved delta-v and revised risk feed back into the graph and the search runs ' +
      'again, which is the only version of this problem that survives contact with reality.',
    inputs: ['Measured state', 'Executed burn', 'Revised risk'],
    output: 'A re-solved plan against the new state',
  },
];

/* --- Objectives -----------------------------------------------------------
   The four terms the engine carries in its objective vector, and only those four. The list is
   transcribed from `cpp/core/ObjectiveVector.hpp`, whose struct fields and
   `kObjectiveCount = 4` are the definition of record, against the units and compositions
   tabulated in docs/OPTIMIZATION_MODEL.md §2.

   Every term is minimised. That is worth stating per row because it is the property the whole
   search rests on: with no "higher is better" exception, the dominance test loops over
   dimensions generically and knows nothing about which dimension it is comparing. A quantity
   where more is better is inverted at its source instead. */

export const OBJECTIVES: readonly Objective[] = [
  {
    key: 'time',
    label: 'Time',
    direction: 'minimise',
    unit: 's',
    note:
      'Transfer plus wait, accumulated additively along the path. Bounded above by the ' +
      'survival window, not by preference.',
  },
  {
    key: 'fuel',
    label: 'Fuel',
    direction: 'minimise',
    unit: "fraction of the vehicle's Δv budget",
    note:
      'Edge Δv divided by the budget that vehicle carries, so the term is dimensionless and ' +
      'two vehicles with different tanks are still compared on one axis.',
  },
  {
    key: 'risk',
    label: 'Risk',
    direction: 'minimise',
    unit: '[0, 1]',
    note:
      'Composed along a path as 1 − Π(1 − rᵢ) rather than summed, because independent per-edge ' +
      'risks compound in survival terms and a sum would leave the interval.',
  },
  {
    key: 'distance',
    label: 'Distance',
    direction: 'minimise',
    unit: 'km',
    note:
      'Path length in ECI. It earns a separate axis by disagreeing with fuel: two vehicles ' +
      'close in kilometres but in different planes are expensive in Δv, and a separate term ' +
      'exposes that rather than averaging it away.',
  },
];

export const OBJECTIVES_SOURCE = 'cpp/core/ObjectiveVector.hpp, docs/OPTIMIZATION_MODEL.md §2';

/** §2.1's own answer to the obvious next question, kept beside the list because a reader who
 * counts four objectives asks it immediately. Rendered, not commented out: the page's argument
 * is stronger for naming the limit than for leaving it to be noticed. */
export const OBJECTIVES_LIMIT_NOTE =
  'Four, and not more by choice. Dominance weakens as the objective count grows — fewer labels ' +
  'are comparable, so less is pruned and the label set expands. The search is dimension-generic, ' +
  'so a fifth term is a configuration change, but it would be a measured decision rather than a ' +
  'free one.';

/* --- Capabilities --------------------------------------------------------- */

export const CAPABILITIES: readonly Capability[] = [
  {
    key: 'multi-objective',
    title: 'A frontier, not a weighted sum',
    body:
      'Collapsing several objectives into one weighted score decides the trade before the ' +
      'search starts and hides that decision inside a constant. ORCA keeps the objectives ' +
      'separate through the search and returns every nondominated plan, so the trade is made ' +
      'once, in the open, by a person who can defend it.',
    tags: ['Martins label-setting', 'Pareto dominance', 'No scalarisation'],
  },
  {
    key: 'assignment',
    title: 'Optimal assignment with a baseline to argue against',
    body:
      'When several vehicles can serve several emergencies, the Hungarian algorithm solves the ' +
      'assignment exactly in O(n³). A greedy assigner runs alongside it as a control, because ' +
      'a claim of optimality is only worth as much as the baseline it was measured against.',
    tags: ['Hungarian O(n³)', 'Greedy control', 'Regret measured'],
  },
  {
    key: 'scale',
    title: 'Built for the sizes the problem actually reaches',
    body:
      'At 1,000 objects the transfer graph holds 1,125 nodes and 1,264,500 edges and is ' +
      'constructed in roughly 402–409 ms. Rendering that many objects needed instancing and a ' +
      'uniform-grid spatial index rather than a per-object mesh and an O(N²) proximity scan.',
    tags: ['1,264,500 edges', 'InstancedMesh', 'Uniform grid'],
    source: 'docs/FINAL_BENCHMARK_REPORT.md §5',
  },
  {
    key: 'evidence',
    title: 'Every claim traced to a measurement',
    body:
      'The benchmark suite emits 972 search rows and 144 assignment rows from a single ' +
      'reproducible run, and the evaluation reports the results that went against the ' +
      'hypothesis as prominently as the ones that supported it.',
    tags: ['972 search rows', '144 assignment rows', 'Caveats published'],
    source: 'docs/FINAL_BENCHMARK_REPORT.md §3',
  },
];

/* --- Measurement environment ---------------------------------------------
   Quoted so the timings below are interpretable. A millisecond figure without a machine is a
   number, not a measurement. */

export const MEASUREMENT_ENVIRONMENT = {
  machine: 'Apple M4, 10 cores, 16 GB',
  os: 'macOS (Darwin 24.6.0, arm64)',
  compiler: 'Apple clang 17.0.0',
  flags: '-std=c++20 -O2 -DNDEBUG, warnings-as-errors (-Wall -Wextra -Wpedantic -Werror)',
  date: '2026-09-03',
  suite: '972 search rows and 144 assignment rows; full default suite wall-clock 7.2 s',
  source: 'docs/FINAL_BENCHMARK_REPORT.md §2, §3',
} as const;

/* --- Research results ----------------------------------------------------- */

export const RESEARCH_RESULTS: readonly ResearchResult[] = [
  {
    id: 'search-efficiency',
    question: 'Does an admissible heuristic reduce the work a shortest-path search does?',
    finding:
      'Yes, and the advantage grows with the graph. A* expands 18.2% fewer nodes than Dijkstra ' +
      'at 25 objects, 36.2% fewer at 50 and 43.6% fewer at 100. The heuristic is built from ' +
      'Hohmann and plane-change lower bounds, so it never overestimates and the result stays ' +
      'optimal.',
    caveat:
      'At 25 objects A* is marginally slower in wall clock — 0.01334 ms against 0.01330 ms. ' +
      'The per-node heuristic evaluation costs about as much as the two nodes it saves, so the ' +
      'node reduction does not become a time reduction until the graph is larger.',
    source: 'docs/RESEARCH_EVALUATION.md §5.1',
    table: {
      caption: 'Nodes expanded, mean ± population standard deviation, n = 90 per cell',
      columns: ['Objects', 'Dijkstra', 'A*', 'Fewer expansions'],
      rows: [
        ['25', '11.00 ± 6.54', '9.00 ± 6.84', '18.2%'],
        ['50', '30.83 ± 15.35', '19.67 ± 15.21', '36.2%'],
        ['100', '53.00 ± 31.33', '29.89 ± 21.41', '43.6%'],
      ],
    },
  },
  {
    id: 'multi-objective-cost',
    question: 'What does multi-objective planning cost against single-objective search?',
    finding:
      'A great deal, and disproportionately more as the problem grows. Label-setting Pareto ' +
      'search runs 9.5× the cost of Dijkstra at 25 objects, 13.4× at 50 and 30.4× at 100. ' +
      'Across a 4× increase in object count Dijkstra grows 14.4×, A* 11.5× and Pareto 45.9× — ' +
      'the clearest quantitative confirmation that label-setting is the most expensive ' +
      'algorithm in the codebase.',
    caveat:
      'This is the price of the frontier, and it is charged whether or not the frontier is ' +
      'used. A deployment that only ever needs one weighted answer should run Dijkstra or A* ' +
      'and not pay it.',
    source: 'docs/RESEARCH_EVALUATION.md §5.2',
    table: {
      caption: 'Mean execution time, milliseconds',
      columns: ['Objects', 'Dijkstra', 'A*', 'Pareto', 'Pareto ÷ Dijkstra'],
      rows: [
        ['25', '0.01330', '0.01334', '0.12637', '9.5×'],
        ['50', '0.06482', '0.05973', '0.87109', '13.4×'],
        ['100', '0.19105', '0.15336', '5.80208', '30.4×'],
        ['25 → 100 growth', '14.4×', '11.5×', '45.9×', '—'],
      ],
    },
  },
  {
    id: 'assignment-quality',
    question: 'Is optimal assignment worth its cost against a greedy baseline?',
    finding:
      'Yes, once the comparison is made fairly. Restricted to the runs where both algorithms ' +
      'served the same number of emergencies, Hungarian is never worse than greedy: regret is ' +
      'greater than or equal to zero in every such run.',
    caveat:
      'That statement rests on 15 of 60 paired runs. The naive all-60 comparison showed a mean ' +
      'regret of −0.54, which appears to favour greedy — but only because greedy served fewer ' +
      'emergencies and was therefore being scored on an easier problem. The flattering reading ' +
      'was wrong and was corrected before publication; the corrected figure is the one quoted ' +
      'here, and the n = 15 restriction is part of the claim.',
    source: 'docs/RESEARCH_EVALUATION.md §5.3',
    table: {
      caption: 'Hungarian regret against the greedy baseline',
      columns: ['Comparison set', 'Paired runs', 'Mean regret', 'What it supports'],
      rows: [
        ['All paired runs', '60', '−0.54', 'Nothing: greedy served fewer emergencies'],
        ['Equal emergencies served', '15', '+0.1388', 'Hungarian is never worse than greedy'],
      ],
    },
  },
  {
    id: 'render-cost',
    question: 'What limits rendering performance at constellation scale?',
    finding:
      'Per-object meshes. Measured in a real browser with Playwright against an instrumented ' +
      'WebGLRenderingContext, a generated 500-satellite / 62-vehicle scenario issued 1,128 draw ' +
      'calls per frame with one mesh per marker. Replacing those with one InstancedMesh per ' +
      'marker kind brought it to 568, a 49.6% reduction.',
    caveat:
      'The remaining count is now dominated by a different O(N) cost that was not fixed: orbit ' +
      'rings are still one draw call each and account for roughly 566 of the 568. The ' +
      'instancing work moved the bottleneck rather than removing it.',
    source: 'docs/FINAL_BENCHMARK_REPORT.md §6',
    table: {
      caption: 'Draw calls per frame, 500 satellites and 62 vehicles',
      columns: ['Marker strategy', 'Draw calls / frame', 'Change'],
      rows: [
        ['One mesh per marker', '1,128', 'Baseline'],
        ['One InstancedMesh per kind', '568', '−49.6%'],
        ['Of which orbit rings', '~566', 'Not addressed'],
      ],
    },
  },
];

/* --- Architecture ---------------------------------------------------------
   The three levels and the one-word roles the README gives them. The ordering here is
   presentation-first because that is the order a reader of this page meets them. */

export const ARCHITECTURE_LEVELS: readonly ArchitectureLevel[] = [
  {
    level: 'Level 3',
    name: 'Presentation',
    stack: 'React · TypeScript · Three.js',
    role: 'the eyes',
    body:
      'Displays engine state and never computes a value the engine could compute. Every number ' +
      'in the workspace arrives from a snapshot; none is derived in the browser to fill a gap.',
  },
  {
    level: 'Level 2',
    name: 'Bridge',
    stack: 'Node · Express',
    role: 'the nervous system',
    body:
      'Owns the process lifecycle and the REST surface. It transports engine state and ' +
      'commands, and holds no simulation state of its own.',
  },
  {
    level: 'Level 1',
    name: 'Engine',
    stack: 'C++20',
    role: 'the brain',
    body:
      'Owns all state and all computation. It runs standalone, with no Node process and no ' +
      'browser — the interface is an optional consumer, not a dependency.',
  },
];

export const ALGORITHM_LIST: readonly AlgorithmEntry[] = [
  { name: 'Weighted directed graph', role: 'Adjacency-list transfer network over orbital states' },
  { name: 'Binary heap priority queue', role: 'Frontier ordering for every search in the engine' },
  { name: "Dijkstra's algorithm", role: 'Single-objective shortest path, the correctness baseline' },
  { name: 'A* search', role: 'Admissible heuristics from Hohmann and plane-change lower bounds' },
  { name: 'Multi-objective label-setting', role: "Martins' algorithm over the objective vector" },
  { name: 'Pareto dominance pruning', role: 'Discards a label only when another dominates it' },
  { name: 'Hungarian algorithm', role: 'Exact O(n³) vehicle-to-emergency assignment, from scratch' },
  { name: 'Greedy assignment', role: 'Control baseline the optimal assigner is measured against' },
  { name: 'Uniform-grid spatial index', role: 'Debris proximity queries without an O(N²) scan' },
];

/* --- Disclosure -----------------------------------------------------------
   Placed in this file rather than hard-coded in the footer so that it is impossible to ship a
   version of the page where a section quotes a benchmark and the disclosure has drifted. */

export const DISCLOSURE = {
  heading: 'Platform status',
  body:
    'ORCA is a research demonstrator. This page is a static presentation surface: it does not ' +
    'connect to the C++ engine, and no figure shown here is live telemetry. Every measurement ' +
    'is transcribed from the repository documents cited beside it and was produced on the ' +
    'single machine described in the measurement environment; results on other hardware will ' +
    'differ. Interactive elements on this page use clearly labelled illustrative fixtures, not ' +
    'engine output — the engine is reached only from the workspace.',
  claims:
    'No claim of flight readiness, certification, operational qualification or endorsement by ' +
    'any space agency or operator is made or implied. The orbital models are simplified for ' +
    'research purposes and are not a mission-planning authority.',
  source: 'README, docs/ARCHITECTURE.md §6.1',
} as const;

/**
 * The REAL preference profiles ORCA's engine uses for stage-2 plan selection — a verbatim
 * mirror of `configs/profiles.json`, which is the single source of truth (the C++ engine reads
 * that file; `docs/OPTIMIZATION_MODEL.md` §6 specifies it).
 *
 * ## Why a copy rather than an import
 *
 * `configs/` sits above the Vite root (`frontend/`), so importing the JSON directly would put a
 * path outside the served root into the browser bundle. The values are mirrored here instead and
 * `test/preferenceProfiles.test.ts` reads the real `configs/profiles.json` from disk and asserts
 * this file matches it field for field — so drift fails the test suite rather than silently
 * shipping a landing page that misstates how the engine actually selects a plan.
 *
 * ## Why the marketing surface uses the real profiles at all
 *
 * ORCA's central architectural claim is a TWO-STAGE separation: the multi-objective search
 * computes the Pareto front first, and a preference profile only *weights an already-computed
 * front* — it never influences the search (`configs/profiles.json`'s own `$comment`, and
 * `OPTIMIZATION_MODEL.md` §6.2). A landing page that demonstrated selection with invented
 * profile names would be showing a product that does not exist. The candidate front on the
 * landing page is illustrative and labelled as such; the *selection rule applied to it* is the
 * engine's real one.
 */

/** The four objectives ORCA scores a plan against (`OPTIMIZATION_MODEL.md` §6.2's `k = 4`). */
export type ObjectiveKey = 'time' | 'fuel' | 'risk' | 'distance';

export interface ProfileWeights {
  readonly time: number;
  readonly fuel: number;
  readonly risk: number;
  readonly distance: number;
}

export interface PreferenceProfile {
  readonly id: string;
  readonly label: string;
  readonly description: string;
  readonly weights: ProfileWeights;
}

/**
 * Verbatim from `configs/profiles.json`. `CUSTOM` is deliberately excluded from the marketing
 * surface: it exists so an operator can override the weights with sliders in the workspace, and
 * a landing page with no slider would render it as a meaningless fifth button.
 */
export const PREFERENCE_PROFILES: readonly PreferenceProfile[] = [
  {
    id: 'BALANCED',
    label: 'Balanced',
    description: 'Default profile. Equal emphasis on time, fuel and risk.',
    weights: { time: 0.3, fuel: 0.3, risk: 0.3, distance: 0.1 },
  },
  {
    id: 'FASTEST',
    label: 'Fastest',
    description: 'Minimise rescue time. Retains a risk floor so the selection is not reckless.',
    weights: { time: 0.7, fuel: 0.1, risk: 0.15, distance: 0.05 },
  },
  {
    id: 'FUEL_SAVING',
    label: 'Fuel Saving',
    description: 'Preserve vehicle delta-v budget for subsequent missions.',
    weights: { time: 0.1, fuel: 0.7, risk: 0.15, distance: 0.05 },
  },
  {
    id: 'SAFETY_FIRST',
    label: 'Safety First',
    description: 'Minimise route risk, accepting longer transfers.',
    weights: { time: 0.15, fuel: 0.1, risk: 0.7, distance: 0.05 },
  },
] as const;

/** `configs/profiles.json`'s `defaultProfile`. */
export const DEFAULT_PROFILE_ID = 'BALANCED';

/**
 * The engine's tie-breaking order, applied in sequence until one candidate remains
 * (`configs/profiles.json`'s `tieBreaking.order`). `planId` is unique, which is what makes the
 * ordering total rather than merely partial (`OPTIMIZATION_MODEL.md` §10).
 */
export const TIE_BREAKING_ORDER: readonly string[] = ['score', 'risk', 'time', 'fuel', 'planId'];

/**
 * The modelling decision worth surfacing on the landing page: even `FASTEST` keeps a non-zero
 * risk weight. A profile that zeroes an objective selects an extreme point that may be absurd on
 * the ignored axis — the fastest route at any risk. The floor is what makes these behave as
 * *preferences* rather than *lexicographic orderings* (`OPTIMIZATION_MODEL.md` §6.1).
 */
export const RISK_FLOOR_NOTE =
  'Even the Fastest profile retains a non-zero risk weight. A profile that zeroes an objective ' +
  'selects an extreme point that may be absurd on the ignored axis — the fastest route at any ' +
  'risk. The floor is what makes these preferences rather than lexicographic orderings.';

/** Weights are normalised to sum to 1 internally, so only ratios matter (§6.2). */
export function normalisedWeights(weights: ProfileWeights): ProfileWeights {
  const total = weights.time + weights.fuel + weights.risk + weights.distance;
  if (total <= 0) {
    // All-zero and negative weight vectors are rejected at the engine's API boundary
    // (`ARCHITECTURE.md` §11); the marketing surface never produces one, so this is a guard
    // against a future edit rather than a reachable state.
    return weights;
  }
  return {
    time: weights.time / total,
    fuel: weights.fuel / total,
    risk: weights.risk / total,
    distance: weights.distance / total,
  };
}

/**
 * `score(plan) = Σᵢ wᵢ · norm[i](plan)` — the engine's own selection rule
 * (`OPTIMIZATION_MODEL.md` §6.2). `normalised` carries each objective already mapped into [0, 1]
 * with 0 best, which is what `norm[i]` means there; the caller owns that normalisation because
 * it depends on the range of the candidate set being scored.
 *
 * The recommended plan is the `argmin` of this score over the front — lower is better.
 */
export function profileScore(normalised: ProfileWeights, weights: ProfileWeights): number {
  const w = normalisedWeights(weights);
  return (
    w.time * normalised.time +
    w.fuel * normalised.fuel +
    w.risk * normalised.risk +
    w.distance * normalised.distance
  );
}

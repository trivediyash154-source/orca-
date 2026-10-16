/**
 * Colours for the catalogue point field.
 *
 * Hex literals live here rather than in components, matching the convention the existing
 * `visualization/satellites/markerColors.ts` already sets: a WebGL material cannot read a CSS
 * custom property, so the tokens are mirrored in one named place instead of being scattered
 * inline. Each value below corresponds to a token in `styles/tokens.css` and must be changed
 * with it.
 *
 * The mapping is deliberately narrow. A colour on this display is a claim about an object, and
 * `backend/src/catalog/classify.ts` owns which claim applies — this module only decides what
 * that claim looks like.
 */

import { Color } from 'three';

/** Operational payload — `--orca-primary`. The default state of a healthy catalogue object. */
export const NOMINAL_COLOR = new Color('#4a90d6');

/** Debris, spent stages, non-operational and re-entered objects — `--orca-critical`. */
export const ATTENTION_COLOR = new Color('#d6544a');

/** No SATCAT row, or a status the catalog does not state — `--orca-neutral`. Deliberately dim:
 * "we do not know" should not compete visually with "we know this is a hazard". */
export const UNKNOWN_COLOR = new Color('#5a6a80');

/** Inside the active planning corridor — `--orca-success`. Green means mission-relevant, and
 * relevance is computed against a corridor rather than stored on the object. */
export const CANDIDATE_COLOR = new Color('#45b787');

/** The mission's target vehicle — `--orca-warning`, the warm rescue-signal end of the palette. */
export const TARGET_COLOR = new Color('#d6a344');

/** The rescue vehicle — `--orca-mission-active`, matching the engine's own in-transit colour. */
export const RESCUE_COLOR = new Color('#4ac9d6');

/** Selection highlight. White reads as "this one" against every other state. */
export const SELECTED_COLOR = new Color('#ffffff');

export type CatalogClassName = 'NOMINAL' | 'ATTENTION' | 'UNKNOWN';
export type RelevanceName = 'CANDIDATE' | 'TARGET' | 'RESCUE_VEHICLE' | 'NONE';

/**
 * Mission relevance outranks catalogue class when both apply: an operator who has loaded a
 * mission is looking for the objects that serve it, and "this is the target" is more useful than
 * "this is an operational payload". The catalogue class still shows through for everything the
 * mission does not touch.
 */
export function colorFor(catalogClass: CatalogClassName, relevance: RelevanceName): Color {
  switch (relevance) {
    case 'TARGET':
      return TARGET_COLOR;
    case 'RESCUE_VEHICLE':
      return RESCUE_COLOR;
    case 'CANDIDATE':
      return CANDIDATE_COLOR;
    case 'NONE':
      break;
  }
  switch (catalogClass) {
    case 'NOMINAL':
      return NOMINAL_COLOR;
    case 'ATTENTION':
      return ATTENTION_COLOR;
    case 'UNKNOWN':
      return UNKNOWN_COLOR;
  }
}

/**
 * Point diameter as a real world-space size, in Earth radii (the scene's unit). The shader
 * converts these to pixels through the projection, so a point keeps a consistent apparent size
 * as the camera moves rather than being a fixed pixel blob.
 *
 * `base` is ~0.012 Earth radii, about 75 km across. That is far larger than any real
 * spacecraft, and deliberately so: at true scale a satellite is invisible from a viewpoint that
 * shows the whole planet. The field is a legible symbol layer, not a physical rendering, which
 * is why size encodes ROLE here and never anything a reader might mistake for spacecraft
 * dimensions.
 */
export const POINT_SIZE = {
  base: 0.012,
  candidate: 0.018,
  mission: 0.032,
  selected: 0.042,
  hovered: 0.024,
} as const;

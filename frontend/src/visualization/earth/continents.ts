/**
 * Coarse continent outlines as (longitude, latitude) polygons — a DECORATION asset this
 * repo generates itself, not a cartographic dataset and not a simulation input. Nothing in
 * `cpp/` ever reads this; Earth's real radius/rotation and every satellite position still
 * come from the engine untouched (`coordinates.ts`).
 *
 * Why polygons rather than a fetched image: `ARCHITECTURE.md`'s offline commitment means the
 * runtime must not depend on a remote CDN, and no texture is vendored in this repo. Phase
 * 12's earlier attempt used random blobs, which read as noise rather than Earth. Hand-traced
 * outlines are coarse (tens of vertices per landmass, not thousands) but they are the actual
 * shapes, so the planet is immediately recognizable — which is the entire point.
 *
 * Accuracy: approximate to roughly a few degrees. Adequate for a recognizable globe at the
 * zoom levels this app renders; NOT suitable for any geographic claim.
 */

export type LonLat = readonly [number, number];

export const NORTH_AMERICA: readonly LonLat[] = [
  [-168, 65], [-162, 71], [-140, 70], [-125, 70], [-110, 68], [-95, 70], [-85, 70],
  [-80, 63], [-78, 56], [-64, 60], [-55, 52], [-60, 47], [-67, 45], [-70, 42],
  [-75, 35], [-81, 25], [-84, 30], [-90, 29], [-97, 26], [-97, 22], [-105, 20],
  [-110, 24], [-115, 30], [-124, 40], [-125, 48], [-135, 57], [-150, 59], [-165, 55],
];

export const CENTRAL_AMERICA: readonly LonLat[] = [
  [-97, 22], [-94, 18], [-88, 18], [-83, 15], [-79, 9], [-77, 8], [-82, 8],
  [-85, 11], [-90, 14], [-95, 16], [-98, 19],
];

export const SOUTH_AMERICA: readonly LonLat[] = [
  [-77, 8], [-72, 12], [-62, 10], [-55, 5], [-50, 0], [-44, -2], [-35, -5],
  [-38, -13], [-40, -20], [-48, -25], [-53, -34], [-58, -38], [-62, -40], [-65, -45],
  [-68, -50], [-70, -55], [-75, -52], [-73, -45], [-73, -37], [-71, -30], [-70, -20],
  [-75, -14], [-78, -8], [-81, -5], [-80, 0], [-78, 5],
];

export const AFRICA: readonly LonLat[] = [
  [-17, 15], [-10, 20], [0, 25], [10, 32], [20, 32], [30, 31], [33, 28], [36, 22],
  [39, 15], [43, 11], [51, 12], [45, 5], [42, -1], [40, -10], [35, -18], [33, -25],
  [28, -33], [20, -34], [18, -32], [13, -22], [11, -15], [9, -5], [9, 4], [3, 6],
  [-5, 5], [-10, 6], [-13, 10],
];

export const EURASIA: readonly LonLat[] = [
  [-10, 36], [-9, 43], [-2, 43], [0, 49], [4, 53], [8, 54], [10, 57], [13, 55],
  [20, 55], [28, 56], [30, 60], [28, 65], [22, 66], [18, 69], [25, 71], [35, 70],
  [45, 68], [60, 70], [70, 72], [80, 74], [90, 75], [100, 76], [110, 76], [120, 73],
  [130, 71], [140, 72], [150, 70], [160, 69], [170, 68], [179, 66], [179, 62],
  [170, 60], [163, 58], [155, 55], [145, 50], [140, 45], [135, 42], [130, 42],
  [128, 38], [125, 38], [122, 30], [120, 22], [110, 20], [105, 10], [100, 7],
  [95, 15], [90, 22], [88, 21], [80, 15], [75, 8], [72, 20], [68, 24], [62, 25],
  [57, 25], [50, 28], [45, 30], [40, 36], [35, 36], [28, 41], [20, 40], [15, 38],
  [12, 38], [10, 44], [5, 43], [0, 40], [-5, 36],
];

export const AUSTRALIA: readonly LonLat[] = [
  [113, -22], [114, -26], [115, -32], [118, -35], [125, -32], [130, -32], [135, -35],
  [138, -35], [141, -38], [146, -39], [150, -37], [153, -30], [153, -25], [148, -20],
  [145, -15], [142, -11], [136, -12], [132, -11], [130, -12], [126, -14], [122, -17],
  [118, -20],
];

export const GREENLAND: readonly LonLat[] = [
  [-45, 60], [-52, 64], [-55, 70], [-50, 76], [-40, 80], [-25, 82], [-18, 78],
  [-22, 70], [-30, 65], [-40, 60],
];

export const ANTARCTICA: readonly LonLat[] = [
  [-180, -63], [-150, -70], [-120, -73], [-90, -72], [-60, -63], [-30, -68],
  [0, -69], [30, -68], [60, -66], [90, -66], [120, -66], [150, -70], [180, -63],
  [180, -90], [-180, -90],
];

export const MADAGASCAR: readonly LonLat[] = [
  [44, -12], [50, -15], [50, -22], [47, -25], [44, -20], [43, -16],
];

export const BRITAIN: readonly LonLat[] = [
  [-5, 50], [-3, 54], [-3, 58], [-5, 58], [-6, 55], [-5, 52],
];

export const JAPAN: readonly LonLat[] = [
  [130, 32], [135, 34], [140, 36], [142, 40], [141, 45], [138, 42], [135, 36],
  [131, 33],
];

export const NEW_ZEALAND: readonly LonLat[] = [
  [173, -35], [178, -38], [177, -41], [172, -41], [168, -46], [166, -45], [170, -41],
  [172, -37],
];

export const INDONESIA: readonly LonLat[] = [
  [95, 5], [104, 2], [106, -6], [114, -8], [120, -9], [130, -8], [135, -4],
  [140, -3], [134, -1], [125, 1], [117, 4], [110, 2], [100, 6],
];

export const SUMATRA_BORNEO_GAP: readonly LonLat[] = [
  [109, 1], [117, 1], [119, -3], [116, -4], [110, -3],
];

/** Every landmass drawn, largest first so smaller ones overlay cleanly. */
export const LANDMASSES: ReadonlyArray<readonly LonLat[]> = [
  ANTARCTICA,
  EURASIA,
  AFRICA,
  NORTH_AMERICA,
  SOUTH_AMERICA,
  AUSTRALIA,
  GREENLAND,
  CENTRAL_AMERICA,
  INDONESIA,
  SUMATRA_BORNEO_GAP,
  MADAGASCAR,
  JAPAN,
  BRITAIN,
  NEW_ZEALAND,
];

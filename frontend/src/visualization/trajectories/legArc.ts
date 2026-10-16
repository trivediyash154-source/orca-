/**
 * The connector drawn between a mission leg's two endpoints.
 *
 * The engine reports where a leg starts and where it ends, and nothing in between -- exact
 * mid-leg position is not modelled (`DYNAMIC_REPLANNING.md` §13.2). The connector previously
 * honoured that by drawing a straight chord. But both endpoints are in orbit, so for any pair
 * more than a few degrees apart around the planet the chord passes *through* the Earth, which
 * is a stronger and more wrong claim than any curve: a trajectory cannot go through the planet.
 *
 * So the connector now sweeps around the planet instead. The direction is spherically
 * interpolated between the endpoints and the radius linearly, which keeps every sample at or
 * above the lower of the two orbital radii. That is still not a modelled trajectory and is not
 * presented as one; it is the least-assuming curve that does not contradict physics.
 */

export type Vec3 = readonly [number, number, number];

const SEGMENTS = 48;

/** Below this angle the chord and the arc are visually identical, and slerp's `sin(theta)`
 * denominator heads to zero, so the straight segment is returned directly. */
const STRAIGHT_BELOW_RAD = 1e-3;

function length(v: Vec3): number {
  return Math.hypot(v[0], v[1], v[2]);
}

export function legArc(from: Vec3, to: Vec3, segments = SEGMENTS): Vec3[] {
  const r0 = length(from);
  const r1 = length(to);
  if (r0 < 1e-9 || r1 < 1e-9) return [from, to];

  const a: Vec3 = [from[0] / r0, from[1] / r0, from[2] / r0];
  const b: Vec3 = [to[0] / r1, to[1] / r1, to[2] / r1];

  const cos = Math.min(1, Math.max(-1, a[0] * b[0] + a[1] * b[1] + a[2] * b[2]));
  const theta = Math.acos(cos);

  if (theta < STRAIGHT_BELOW_RAD) return [from, to];

  // Exactly antipodal endpoints have no unique great circle. Rather than pick one silently,
  // the plane is fixed by any axis not parallel to `a`, which is deterministic and stays
  // outside the planet like every other case.
  let axis: Vec3 | null = null;
  if (Math.PI - theta < STRAIGHT_BELOW_RAD) {
    const helper: Vec3 = Math.abs(a[0]) < 0.9 ? [1, 0, 0] : [0, 1, 0];
    const cx = a[1] * helper[2] - a[2] * helper[1];
    const cy = a[2] * helper[0] - a[0] * helper[2];
    const cz = a[0] * helper[1] - a[1] * helper[0];
    const n = Math.hypot(cx, cy, cz);
    axis = [cx / n, cy / n, cz / n];
  }

  const points: Vec3[] = [];
  const sinTheta = Math.sin(theta);

  for (let i = 0; i <= segments; i += 1) {
    const t = i / segments;
    let dir: Vec3;

    if (axis !== null) {
      // Rotate `a` about `axis` by `t * pi` (Rodrigues, with axis perpendicular to `a`).
      const angle = t * Math.PI;
      const c = Math.cos(angle);
      const s = Math.sin(angle);
      const kx = axis[1] * a[2] - axis[2] * a[1];
      const ky = axis[2] * a[0] - axis[0] * a[2];
      const kz = axis[0] * a[1] - axis[1] * a[0];
      dir = [a[0] * c + kx * s, a[1] * c + ky * s, a[2] * c + kz * s];
    } else {
      const wa = Math.sin((1 - t) * theta) / sinTheta;
      const wb = Math.sin(t * theta) / sinTheta;
      dir = [a[0] * wa + b[0] * wb, a[1] * wa + b[1] * wb, a[2] * wa + b[2] * wb];
    }

    const r = r0 + (r1 - r0) * t;
    points.push([dir[0] * r, dir[1] * r, dir[2] * r]);
  }

  // The endpoints are the engine's reported positions and must be drawn exactly, not
  // reconstructed through a normalise-and-rescale round trip that drifts in the last digit.
  points[0] = from;
  points[points.length - 1] = to;
  return points;
}

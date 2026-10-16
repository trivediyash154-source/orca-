import { useFrame } from '@react-three/fiber';
import { useRef } from 'react';
import { Vector3 } from 'three';

/**
 * The hero scene's camera.
 *
 * This is a DIRECTED camera, not a user-manipulated one, which is why `OrbitControls` is
 * deliberately absent from the entry surface. Two concrete reasons beyond the aesthetic one:
 * `OrbitControls` calls `preventDefault()` on wheel events, so a full-bleed canvas behind the
 * hero copy would eat the page scroll; and a reader who accidentally drags the marketing globe
 * into a useless angle has no way to know they broke the shot. The workspace scene
 * (`visualization/scene/Scene.tsx`) is the surface where the reader steers.
 *
 * ## Framing
 *
 * The rig aims at a FOCUS POINT, not at the world origin. Aiming exactly at the origin would
 * centre Earth in the frame, and both compositions want it off-centre — low-left in the hero,
 * bottom-weighted in the narrow auth column — so the focus sits a fraction of a scene unit
 * above and beside the planet. Nothing in the scene moves to achieve this: Earth stays at the
 * origin at radius 1, which is the invariant every other layer places itself against.
 */

export type HeroCameraVariant = 'hero' | 'auth';

export interface HeroCameraFraming {
  /** Base camera station, in scene units. */
  readonly x: number;
  readonly y: number;
  readonly z: number;
  /** Point the camera aims at. See the framing note above. */
  readonly focusX: number;
  readonly focusY: number;
  readonly focusZ: number;
  readonly fov: number;
}

/**
 * Exported so the `<Canvas>` can be constructed with the correct camera on its very first
 * frame. If the rig alone set the station, the first rendered frame would show the r3f default
 * camera and the second would show this one — a visible snap on load, and under
 * `frameloop="demand"` (reduced motion) possibly the only frame ever rendered.
 */
export const HERO_FRAMING: Readonly<Record<HeroCameraVariant, HeroCameraFraming>> = {
  // Wide three-quarter view. The station is off to the +X side and slightly above the
  // equator, so the terminator runs diagonally across the disc rather than vertically.
  //
  // The stand-off distance is ~3.7 Earth radii, which puts the disc at roughly four fifths of
  // the frame height. That closeness is the whole shot: at the 5+ radii a "safe" framing
  // suggests, Earth renders as a small floating marble in a field of black and the scene
  // stops reading as a place the vehicle is operating in.
  //
  // `focusZ` is non-zero because the offset that moves Earth off centre has to be applied in
  // the CAMERA's basis, not the world's; with the station off the +X side, that basis has a
  // real Z component. Solving it in world axes alone skews the horizon instead of panning.
  //
  // ## Which way the scene is pushed, and why it is this way
  //
  // Aiming at +X drives the scene toward -X on screen. `focusX` was +0.72, which put the disc
  // at page x 90-733 at 1440x900 -- underneath the copy column (x 117-737) rather than beside
  // it. `hero.css`'s scrim suppresses the render from the left edge to roughly 58% of the
  // width so the headline stays readable over it, so the whole mission was rendering inside
  // the region the scrim exists to darken, while the seven callouts (leader tips at x 936-1238)
  // pointed into empty black. Both halves of the composition were failing at once.
  //
  // Mirroring the aim puts the disc at x 602-1223, which brackets the leader tips and leaves
  // the type on the near-opaque end of the scrim. The limb still passes behind the last ~135px
  // of the copy column, which is wanted: it reads as depth under a 50%-opacity scrim, whereas
  // a scene that clears the type entirely reads as two unrelated panels. The annotations were
  // not moved to meet it -- pinned by percentage, they track the render by design, and moving
  // them left would have put them on the headline.
  hero: { x: 2.33, y: 0.86, z: 3.06, focusX: -0.55, focusY: 0.56, focusZ: 0.35, fov: 40 },
  // A narrow column has no horizontal room to fill, so the offset is almost entirely vertical:
  // Earth drops into the bottom half and the upper half is left for the transfer arc. The
  // stand-off is LONGER than the hero's, not shorter — a tall column's horizontal field of view
  // is a fraction of the hero's, so the hero's distance would crop the planet on all four sides
  // and leave a wall of blue behind the form.
  auth: { x: 1.45, y: 0.86, z: 4.58, focusX: 0.05, focusY: 0.95, focusZ: -0.05, fov: 40 },
};

export interface HeroCameraRigProps {
  /** Normalised pointer offset from the container centre, [-1, 1]. */
  readonly parallaxX: number;
  readonly parallaxY: number;
  /** How far the hero has scrolled out of view, [0, 1]. */
  readonly scrollProgress: number;
  readonly reducedMotion: boolean;
  readonly variant?: HeroCameraVariant;
}

/** Pointer travel, in scene units, at full deflection. Small on purpose: the effect has to
 * read as the frame having depth, not as the camera being dragged. */
const PARALLAX_AMPLITUDE_X = 0.3;
const PARALLAX_AMPLITUDE_Y = 0.2;

/** Where the camera goes as the hero scrolls away: up, back, and slightly across. The planet
 * recedes as the reader leaves it, which is what makes the scroll feel like departure rather
 * than like a background image sliding off. */
const SCROLL_LIFT = 0.95;
const SCROLL_DOLLY = 1.55;
const SCROLL_DRIFT_X = -0.55;

/**
 * Convergence rate of the follow, in inverse seconds: the camera closes ~1 - e^-3.4 ≈ 97% of
 * the remaining distance per second.
 *
 * The delta correction is the point. A bare `position.lerp(target, 0.08)` is frame-rate
 * DEPENDENT — it settles twice as fast on a 120 Hz display as on a 60 Hz one, and crawls on a
 * frame that took 100 ms — so the same code produces a different feel on every machine.
 * `1 - exp(-lambda * dt)` is the closed-form step of the equivalent continuous first-order
 * system, which converges at the same rate in seconds regardless of how the seconds are
 * divided into frames. It approaches the target monotonically and never overshoots, which is
 * the behaviour asked for here: instrumentation that springs reads as a toy.
 */
const FOLLOW_LAMBDA = 3.4;

/** See `HeroEarth.tsx` — without this, returning to a backgrounded tab snaps the camera. */
const MAX_FRAME_DELTA_SEC = 0.05;

export function HeroCameraRig({
  parallaxX,
  parallaxY,
  scrollProgress,
  reducedMotion,
  variant = 'hero',
}: HeroCameraRigProps) {
  const framing = HERO_FRAMING[variant];
  // Allocated once and mutated in place: `useFrame` runs up to 120 times a second, and two
  // fresh Vector3s per frame is the kind of garbage that shows up as periodic stutter.
  const targetRef = useRef(new Vector3());
  const focusRef = useRef(new Vector3());

  useFrame((state, delta) => {
    const progress = Math.min(1, Math.max(0, scrollProgress));

    // Under reduced motion the target is the base framing exactly: no pointer response, no
    // scroll response. The reader asked for a still image and gets one.
    const target = targetRef.current;
    if (reducedMotion) {
      target.set(framing.x, framing.y, framing.z);
    } else {
      target.set(
        framing.x + parallaxX * PARALLAX_AMPLITUDE_X + progress * SCROLL_DRIFT_X,
        // Inverted: moving the pointer down raises the camera, so the scene tips toward the
        // pointer the way a held object would.
        framing.y - parallaxY * PARALLAX_AMPLITUDE_Y + progress * SCROLL_LIFT,
        framing.z + progress * SCROLL_DOLLY,
      );
    }

    if (reducedMotion) {
      state.camera.position.copy(target);
    } else {
      state.camera.position.lerp(
        target,
        1 - Math.exp(-FOLLOW_LAMBDA * Math.min(delta, MAX_FRAME_DELTA_SEC)),
      );
    }

    focusRef.current.set(framing.focusX, framing.focusY, framing.focusZ);
    state.camera.lookAt(focusRef.current);
  });

  return null;
}

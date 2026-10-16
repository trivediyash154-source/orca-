import { Stars } from '@react-three/drei';
import { Canvas, useThree } from '@react-three/fiber';
import { memo, useEffect, useState } from 'react';

import { DebrisField } from './DebrisField';
import { HERO_FRAMING, HeroCameraRig } from './HeroCameraRig';
import { HeroEarth } from './HeroEarth';
import { MissionLayer } from './MissionLayer';
import { ProbeRipples } from './ProbeRipples';
import { SceneFallback } from './SceneFallback';
import { SCENE_COLORS } from './spectral';
import { useDocumentVisible } from '../../hooks/useDocumentVisible';
import { usePointerParallax } from '../../hooks/usePointerParallax';
import { usePrefersReducedMotion } from '../../hooks/usePrefersReducedMotion';
import { useWebGLSupport } from '../../hooks/useWebGLSupport';

/**
 * The entry surface's 3D scene, and the single component the rest of the page mounts.
 *
 * It owns the four decisions no section should have to repeat: whether WebGL exists at all,
 * whether the reader has asked for reduced motion, whether the frame loop should be running at
 * this instant, and how much geometry this device can afford. Consumers drop it into a
 * positioned container and pass a variant.
 *
 * ## The frame loop is gated, not decorative
 *
 * `frameloop` resolves to one of three states:
 *
 *   'never'  — the tab is hidden, or the scene has scrolled out of view. A WebGL loop running
 *              behind a hidden tab burns battery for nothing and is audible on a laptop.
 *   'demand' — reduced motion. The scene renders exactly one frame, on an explicit
 *              `invalidate()`, and then stops. The reader gets the composition as a still
 *              image rather than as an animation they asked not to see.
 *   'always' — visible, in view, motion allowed.
 *
 * ## Accessibility
 *
 * The canvas is decorative to assistive technology — `aria-hidden`, with a visually hidden
 * sibling paragraph carrying what the scene actually depicts, because "canvas" announced alone
 * tells a screen-reader user nothing. The SVG fallback is the opposite: it is a real image, so
 * it keeps its own `role="img"` with a title and description and is NOT hidden.
 *
 * ## The measurement nudge
 *
 * `nudgeCanvasMeasurement()` below is not decoration; without it this scene renders nothing in
 * roughly three development loads out of four. r3f v8 only builds its renderer once
 * `react-use-measure` reports a non-zero container size, and that measurement arrives from a
 * single `ResizeObserver` delivery. `react-use-measure` drops any delivery that lands while its
 * internal mounted flag is false, and React's StrictMode runs mount -> cleanup -> mount in
 * development, which opens exactly such a window. If the one delivery falls inside it, the
 * observed size never changes again, so no further delivery is ever scheduled: measurement
 * stays at zero forever, the canvas stays at its default 300x150, and NOTHING IS LOGGED —
 * the scene is simply blank.
 *
 * This scene loses that race far more often than the workspace's canvas does, because it
 * mounts into a container that is already laid out at full size (the WebGL probe has to resolve
 * first), while the workspace canvas mounts before its own layout settles and is measured again
 * naturally. It matters because `src/main.tsx` renders the whole application inside
 * `<StrictMode>`.
 *
 * `react-use-measure` also re-measures on `window` resize, so a synthetic resize event after
 * mount re-takes the measurement through a public API rather than through library internals. It
 * is idempotent: if the size was already read, the recomputed bounds are identical and no state
 * update occurs, so the nudge is a no-op on every load that did not need it.
 *
 * ## Pointer events
 *
 * The stage is `pointer-events: none` and the canvas inside it is `pointer-events: auto` (see
 * `scene.css`), which leaves click-to-probe working while keeping the scene from stealing
 * clicks meant for the hero copy layered above it. `OrbitControls` is deliberately not mounted
 * anywhere on this surface: it calls `preventDefault()` on wheel events, and a full-bleed
 * canvas that eats the page scroll is a broken page.
 */

export type HeroSceneVariant = 'hero' | 'auth';

export interface OrbitalHeroSceneProps {
  readonly variant?: HeroSceneVariant;
  readonly className?: string;
}

type FrameLoop = 'always' | 'demand' | 'never';

/** What a screen reader gets in place of the canvas. It describes the MEANING of the scene —
 * who is moving, toward what, and why the arc is coloured — not its geometry. */
const SCENE_DESCRIPTION =
  'Illustration: a rescue vehicle climbs a computed transfer arc from a low orbital shell ' +
  'toward a disabled satellite on a higher shell above Earth, while orbital debris drifts ' +
  'across the shells between them. The arc is coloured from cyan to warm white along its ' +
  'length to show progress through the manoeuvre. The scene is illustrative and is not ' +
  'engine output.';

/** Inner and outer radius of the populated debris band, in scene units (Earth = 1). Module-level
 * so its identity is stable: an inline tuple would be a new array on every render and would
 * rebuild the entire debris buffer on every pointer move. */
const HERO_DEBRIS_RADIUS: readonly [number, number] = [1.16, 1.72];
const AUTH_DEBRIS_RADIUS: readonly [number, number] = [1.14, 1.6];

interface SceneTier {
  readonly isMobile: boolean;
  readonly maxDpr: number;
  readonly debrisCount: number;
  readonly starCount: number;
  readonly earthSegments: number;
  readonly antialias: boolean;
}

function matchesCompactViewport(): boolean {
  if (typeof window === 'undefined' || typeof window.matchMedia !== 'function') return false;
  try {
    return window.matchMedia('(max-width: 768px)').matches;
  } catch {
    // jsdom and some embedded webviews throw on an unrecognised feature query. An unknown
    // viewport is treated as the desktop case, matching what `useWebGLSupport` would find.
    return false;
  }
}

/**
 * Sampled once, at mount. Re-tiering mid-session would rebuild every geometry buffer in the
 * scene, and the only event that would trigger it is a device rotation crossing 768px — a case
 * where a slightly over-generous debris count costs far less than a full rebuild mid-scroll.
 *
 * Pixel ratio participates because it is the multiplier on everything: a phone at dpr 3 is
 * filling nine times the fragments of a dpr 1 panel of the same CSS size, so the geometry
 * budget has to come down even though the viewport did not.
 */
function readSceneTier(): SceneTier {
  const isMobile = matchesCompactViewport();
  const pixelRatio =
    typeof window === 'undefined' || !Number.isFinite(window.devicePixelRatio)
      ? 1
      : Math.max(1, window.devicePixelRatio);
  const dense = pixelRatio >= 2;

  if (isMobile) {
    return {
      isMobile: true,
      maxDpr: 1.5,
      debrisCount: dense ? 220 : 320,
      starCount: dense ? 620 : 900,
      earthSegments: 96,
      antialias: false,
    };
  }
  return {
    isMobile: false,
    maxDpr: 2,
    debrisCount: dense ? 760 : 980,
    starCount: dense ? 2200 : 3000,
    earthSegments: 128,
    antialias: true,
  };
}

/** When the measurement nudge fires, in ms after mount. Three attempts rather than one: the
 * window that swallows the measurement is a scheduling artefact, so the reliable fix is to
 * re-ask a few times over the first fifth of a second rather than to guess one delay that
 * clears it on every machine. Each attempt costs one `getBoundingClientRect()` and, once the
 * size is known, changes nothing — the recomputed bounds are identical, so no state update
 * and no reconfigure follows. */
const NUDGE_DELAYS_MS: readonly number[] = [0, 60, 180];

/**
 * Re-takes the canvas container's measurement after the mount has settled. See the note on the
 * measurement nudge in this module's header for why a scene that mounts into an already-sized
 * container needs it. Two animation frames, not one: the first is still inside the commit that
 * StrictMode is about to replay.
 */
function nudgeCanvasMeasurement(): () => void {
  if (typeof window === 'undefined') return () => undefined;
  const timers = NUDGE_DELAYS_MS.map((delay) =>
    window.setTimeout(() => window.dispatchEvent(new Event('resize')), delay),
  );
  return () => timers.forEach((timer) => window.clearTimeout(timer));
}

/**
 * Under `frameloop="demand"` nothing renders until something asks. This asks exactly once per
 * transition into demand mode, which is what produces the single static frame reduced motion
 * is supposed to get — and it re-asks when the loop returns from `"never"`, because frames
 * requested while the loop was off were discarded.
 */
function StaticFrameTrigger({ frameloop }: { readonly frameloop: FrameLoop }) {
  const invalidate = useThree((state) => state.invalidate);
  useEffect(() => {
    if (frameloop === 'demand') invalidate();
  }, [frameloop, invalidate]);
  return null;
}

interface SceneContentsProps {
  readonly variant: HeroSceneVariant;
  readonly reducedMotion: boolean;
  readonly tier: SceneTier;
  readonly debrisRadius: readonly [number, number];
}

/**
 * Memoised, and the reason is measurable: `usePointerParallax` updates on every animation
 * frame while the pointer is moving, so the outer component re-renders at frame rate. Its
 * props here are all primitives or module constants, so the whole scene body bails out of
 * reconciliation and only `HeroCameraRig` — the one part that actually consumes the pointer —
 * re-renders.
 */
const SceneContents = memo(function SceneContents({
  variant,
  reducedMotion,
  tier,
  debrisRadius,
}: SceneContentsProps) {
  return (
    <>
      <color attach="background" args={[SCENE_COLORS.background]} />
      {/* Fog is the same value as the background, so the far field dissolves into the ground
          instead of into a visibly different haze that would draw the canvas's own edge. */}
      <fog attach="fog" args={[SCENE_COLORS.fog, 5.4, 26]} />

      <ambientLight intensity={0.32} color={SCENE_COLORS.ambientLight} />
      {/* The key light is what produces the terminator. Its position is the reason Earth reads
          as a lit sphere rather than a textured ball, so it is placed off to the +X side and
          slightly above, matching the light direction the SVG fallback's radial gradient
          fakes. */}
      <directionalLight position={[6.4, 2.6, 3.2]} intensity={1.85} color={SCENE_COLORS.keyLight} />
      {/* Cool fill from behind and below — earthshine. Low enough that it never lifts the
          night side into visibility on its own. */}
      <pointLight position={[-5.2, -2.4, -4.2]} intensity={0.34} color={SCENE_COLORS.rimLight} />

      <Stars
        radius={92}
        depth={58}
        count={tier.starCount}
        factor={3.1}
        saturation={0}
        fade
        speed={reducedMotion ? 0 : 0.32}
      />

      <HeroEarth reducedMotion={reducedMotion} segments={tier.earthSegments} />
      <DebrisField
        count={tier.debrisCount}
        reducedMotion={reducedMotion}
        radiusRange={debrisRadius}
      />
      <MissionLayer reducedMotion={reducedMotion} variant={variant} />
      <ProbeRipples reducedMotion={reducedMotion} />
    </>
  );
});

export function OrbitalHeroScene({ variant = 'hero', className }: OrbitalHeroSceneProps) {
  const support = useWebGLSupport();
  const reducedMotion = usePrefersReducedMotion();
  const documentVisible = useDocumentVisible();
  const { ref: containerRef, x: parallaxX, y: parallaxY } = usePointerParallax<HTMLDivElement>();

  const [tier] = useState<SceneTier>(readSceneTier);
  const [inView, setInView] = useState(true);
  const [scrollProgress, setScrollProgress] = useState(0);

  // `rootMargin` starts the loop slightly before the scene is on screen, so it is already
  // running (and the camera already eased into place) by the time the reader can see it.
  useEffect(() => {
    const element = containerRef.current;
    if (!element || typeof IntersectionObserver === 'undefined') return undefined;
    const observer = new IntersectionObserver(
      (entries) => {
        const entry = entries[0];
        if (entry) setInView(entry.isIntersecting);
      },
      { rootMargin: '160px 0px' },
    );
    observer.observe(element);
    return () => observer.disconnect();
  }, [containerRef]);

  // Scroll progress drives the camera's departure move. The listener is passive and coalesced
  // to one read per animation frame, and the resulting state is quantised: without the
  // quantisation this would re-render the tree on every scroll pixel for a camera offset the
  // reader cannot perceive.
  useEffect(() => {
    const element = containerRef.current;
    if (!element || typeof window === 'undefined') return undefined;
    let frame: number | null = null;

    function read(): void {
      frame = null;
      const rect = element?.getBoundingClientRect();
      if (!rect) return;
      const span = rect.height || 1;
      const next = Math.min(1, Math.max(0, -rect.top / span));
      setScrollProgress((previous) => (Math.abs(previous - next) < 0.005 ? previous : next));
    }

    function schedule(): void {
      if (frame === null) frame = window.requestAnimationFrame(read);
    }

    read();
    window.addEventListener('scroll', schedule, { passive: true });
    window.addEventListener('resize', schedule);
    return () => {
      window.removeEventListener('scroll', schedule);
      window.removeEventListener('resize', schedule);
      if (frame !== null) window.cancelAnimationFrame(frame);
    };
  }, [containerRef]);

  useEffect(() => {
    if (support !== 'available') return undefined;
    return nudgeCanvasMeasurement();
  }, [support]);

  const active = documentVisible && inView;
  const frameloop: FrameLoop = !active ? 'never' : reducedMotion ? 'demand' : 'always';
  const framing = HERO_FRAMING[variant];
  const debrisRadius = variant === 'auth' ? AUTH_DEBRIS_RADIUS : HERO_DEBRIS_RADIUS;
  const probing = support === 'probing';

  const rootClass = ['mk-scene', `mk-scene--${variant}`, className].filter(Boolean).join(' ');

  if (support !== 'available') {
    return (
      <div ref={containerRef} className={rootClass}>
        {/* During the probe the fallback is rendered but not shown and not announced: it
            occupies the same absolutely-positioned box, so resolving to WebGL one frame later
            causes no layout shift and no flash of a diagram that was never going to be used. */}
        <div
          className={probing ? 'mk-scene__fallback-wrap is-probing' : 'mk-scene__fallback-wrap'}
          aria-hidden={probing || undefined}
        >
          <SceneFallback variant={variant} />
        </div>
      </div>
    );
  }

  return (
    <div ref={containerRef} className={rootClass}>
      <div className="mk-scene__stage" aria-hidden="true">
        <Canvas
          frameloop={frameloop}
          dpr={[1, tier.maxDpr]}
          camera={{
            position: [framing.x, framing.y, framing.z],
            fov: framing.fov,
            near: 0.1,
            far: 320,
          }}
          gl={{ antialias: tier.antialias, powerPreference: 'high-performance', alpha: false }}
        >
          {/* Tone mapping is left at r3f's default so Earth matches the workspace globe, which
              was lit against it. Materials that ENCODE a colour opt out individually with
              `toneMapped={false}` — the same convention `EntityMarkerField` established, and
              the reason a spectral stop renders as the token value it stands for. */}
          <SceneContents
            variant={variant}
            reducedMotion={reducedMotion}
            tier={tier}
            debrisRadius={debrisRadius}
          />
          <HeroCameraRig
            parallaxX={parallaxX}
            parallaxY={parallaxY}
            scrollProgress={scrollProgress}
            reducedMotion={reducedMotion}
            variant={variant}
          />
          <StaticFrameTrigger frameloop={frameloop} />
        </Canvas>
      </div>
      <p className="mk-scene__sr">{SCENE_DESCRIPTION}</p>
    </div>
  );
}

/**
 * The Optimization Lab's constellation network: 500 to 2,000 satellites, the inter-satellite
 * links between them, and a rescue route searched across those links — live.
 *
 * Positions advance on closed-form circular orbits (`demoEngine.positionFor`); the link graph is
 * rebuilt a few times a second as geometry changes; Dijkstra and A* are both re-run on every
 * rebuild so their costs can be compared on the same graph at the same instant. Every number
 * in the stats column is a counter from that computation, made in this browser tab.
 *
 * Rendering is 2-D canvas with an orthographic projection rather than WebGL. Two thousand
 * points and a few thousand line segments is well inside what canvas draws at 60 fps, and it
 * keeps this panel independent of the Mission Control scene's GPU context — a phone that can
 * run one WebGL context comfortably is not guaranteed a second.
 */

import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import type { PointerEvent as ReactPointerEvent } from 'react';

import { usePrefersReducedMotion } from '../../hooks/usePrefersReducedMotion';
import { demoVisualConstellation } from '../../services/demoConstellation';
import { positionFor } from '../../services/demoEngine';
import { EARTH_RADIUS_KM, aStar, buildLinks, components, dijkstra } from './constellationGraph';
import type { LinkGraph, SearchResult, Vec3 } from './constellationGraph';

const SIZES = [500, 1000, 2000] as const;
const RANGES = [1_000, 1_500, 2_200] as const;
const DEGREES = [2, 4, 6] as const;

/** Simulation seconds per real second. 60x shows a LEO satellite cross a visible fraction of
 * its orbit in a few seconds without the link graph churning faster than the eye can follow. */
const SIM_RATE = 60;
const REBUILD_MS = 450;

interface Element {
  readonly id: string;
  readonly altitudeKm: number;
  readonly inclinationDeg: number;
  readonly raanDeg: number;
  readonly phaseDeg: number;
  readonly degraded: boolean;
}

interface Frame {
  readonly positions: Vec3[];
  readonly graph: LinkGraph;
  readonly componentCount: number;
  readonly largestComponent: number;
  readonly dijkstra: SearchResult | null;
  readonly aStar: SearchResult | null;
}

function readToken(name: string, fallback: string): string {
  if (typeof document === 'undefined') return fallback;
  const value = getComputedStyle(document.documentElement).getPropertyValue(name).trim();
  return value.length > 0 ? value : fallback;
}

function seededPick(count: number, seed: number): [number, number] {
  // Two well-separated indices, deterministic in the seed, so "new emergency" is reproducible.
  const a = Math.abs(Math.imul(seed + 1, 2_654_435_761)) % count;
  const b = (a + Math.floor(count * (0.42 + ((seed * 7) % 11) / 60))) % count;
  return [a, b === a ? (a + 1) % count : b];
}

export function ConstellationNetwork() {
  const reducedMotion = usePrefersReducedMotion();
  const [size, setSize] = useState<(typeof SIZES)[number]>(1000);
  const [rangeKm, setRangeKm] = useState<(typeof RANGES)[number]>(1_500);
  const [degree, setDegree] = useState<(typeof DEGREES)[number]>(4);
  const [shown, setShown] = useState<'DIJKSTRA' | 'ASTAR'>('ASTAR');
  const [seed, setSeed] = useState(3);
  const [running, setRunning] = useState(true);
  const [frame, setFrame] = useState<Frame | null>(null);

  const canvasRef = useRef<HTMLCanvasElement>(null);
  const wrapRef = useRef<HTMLDivElement>(null);
  const simTimeRef = useRef(0);
  const yawRef = useRef(0.6);
  const pitchRef = useRef(0.35);
  const dragRef = useRef<{ x: number; y: number; yaw: number; pitch: number } | null>(null);
  const frameRef = useRef<Frame | null>(null);

  /** Only the LEO shells take part: a MEO or GEO satellite is tens of thousands of km from
   * everything else in the graph and would only ever appear as an isolated component. */
  const elements = useMemo<Element[]>(() => {
    const constellation = demoVisualConstellation(Math.round(size / 0.82), 7, 0);
    return constellation.satellites
      .filter((satellite) => satellite.band === 'LEO')
      .slice(0, size)
      .map((satellite) => ({
        id: satellite.id,
        altitudeKm: satellite.altitudeKm ?? 550,
        inclinationDeg: satellite.inclinationDeg ?? 53,
        raanDeg: satellite.raanDeg ?? 0,
        phaseDeg: satellite.phaseDeg ?? 0,
        degraded: satellite.status === 'WARNING',
      }));
  }, [size]);

  const [source, target] = useMemo(() => seededPick(elements.length, seed), [elements.length, seed]);

  const positionsAt = useCallback(
    (timeSec: number): Vec3[] =>
      elements.map((e) => positionFor(e.altitudeKm, e.inclinationDeg, e.raanDeg, e.phaseDeg, timeSec)),
    [elements],
  );

  const rebuild = useCallback(() => {
    const positions = positionsAt(simTimeRef.current);
    const graph = buildLinks(positions, rangeKm, degree);
    const summary = components(positions.length, graph.links);
    const next: Frame = {
      positions,
      graph,
      componentCount: summary.count,
      largestComponent: summary.largest,
      dijkstra: dijkstra(positions, graph, source, target),
      aStar: aStar(positions, graph, source, target),
    };
    frameRef.current = next;
    setFrame(next);
  }, [positionsAt, rangeKm, degree, source, target]);

  // Rebuild immediately on any input change, then on a cadence while running.
  useEffect(() => {
    rebuild();
    if (!running) return undefined;
    const interval = setInterval(rebuild, reducedMotion ? REBUILD_MS * 3 : REBUILD_MS);
    return () => clearInterval(interval);
  }, [rebuild, running, reducedMotion]);

  // Draw loop. Positions are re-evaluated per frame so motion is smooth; links come from the
  // most recent rebuild and are drawn between the CURRENT positions of their endpoints.
  useEffect(() => {
    let raf = 0;
    let last = typeof performance === 'undefined' ? 0 : performance.now();

    const draw = (timestamp: number) => {
      const canvas = canvasRef.current;
      const wrap = wrapRef.current;
      const current = frameRef.current;
      if (canvas && wrap && current) {
        const dt = Math.min(0.1, (timestamp - last) / 1000);
        last = timestamp;
        if (running) {
          simTimeRef.current += dt * SIM_RATE;
          if (!reducedMotion && dragRef.current === null) yawRef.current += dt * 0.06;
        }
        paint(canvas, wrap, positionsAt(simTimeRef.current), current, shown, source, target, yawRef.current, pitchRef.current);
      }
      raf = requestAnimationFrame(draw);
    };

    raf = requestAnimationFrame(draw);
    return () => cancelAnimationFrame(raf);
  }, [positionsAt, running, reducedMotion, shown, source, target]);

  function onPointerDown(event: ReactPointerEvent<HTMLCanvasElement>): void {
    event.currentTarget.setPointerCapture(event.pointerId);
    dragRef.current = { x: event.clientX, y: event.clientY, yaw: yawRef.current, pitch: pitchRef.current };
  }

  function onPointerMove(event: ReactPointerEvent<HTMLCanvasElement>): void {
    const drag = dragRef.current;
    if (drag === null) return;
    yawRef.current = drag.yaw + (event.clientX - drag.x) * 0.008;
    pitchRef.current = Math.max(-1.3, Math.min(1.3, drag.pitch + (event.clientY - drag.y) * 0.008));
  }

  function onPointerUp(): void {
    dragRef.current = null;
  }

  const result = frame ? (shown === 'ASTAR' ? frame.aStar : frame.dijkstra) : null;
  const links = frame?.graph.links.length ?? 0;
  const meanDegree = frame && frame.positions.length > 0 ? (2 * links) / frame.positions.length : 0;
  const saving =
    frame?.dijkstra && frame.aStar && frame.dijkstra.nodesExpanded > 0
      ? 1 - frame.aStar.nodesExpanded / frame.dijkstra.nodesExpanded
      : null;

  return (
    <section className="lab-panel net">
      <header className="net__head">
        <div>
          <h2>Constellation link network</h2>
          <p className="muted">
            {elements.length.toLocaleString()} LEO satellites, linked to their nearest visible neighbours
            within range, with a rescue route searched across the links. Computed live in this browser:
            every link, component and route below is real output, not replayed telemetry.
          </p>
        </div>
        <div className="net__controls" role="group" aria-label="Network settings">
          <label>
            Satellites
            <select value={size} onChange={(e) => setSize(Number(e.target.value) as (typeof SIZES)[number])}>
              {SIZES.map((value) => (
                <option key={value} value={value}>
                  {value.toLocaleString()}
                </option>
              ))}
            </select>
          </label>
          <label>
            Link range
            <select value={rangeKm} onChange={(e) => setRangeKm(Number(e.target.value) as (typeof RANGES)[number])}>
              {RANGES.map((value) => (
                <option key={value} value={value}>
                  {value.toLocaleString()} km
                </option>
              ))}
            </select>
          </label>
          <label>
            Links per sat
            <select value={degree} onChange={(e) => setDegree(Number(e.target.value) as (typeof DEGREES)[number])}>
              {DEGREES.map((value) => (
                <option key={value} value={value}>
                  {value}
                </option>
              ))}
            </select>
          </label>
          <label>
            Route by
            <select value={shown} onChange={(e) => setShown(e.target.value as 'DIJKSTRA' | 'ASTAR')}>
              <option value="ASTAR">A*</option>
              <option value="DIJKSTRA">Dijkstra</option>
            </select>
          </label>
          <button type="button" onClick={() => setSeed((value) => value + 1)}>
            New emergency
          </button>
          <button type="button" onClick={() => setRunning((value) => !value)} aria-pressed={!running}>
            {running ? 'Pause' : 'Resume'}
          </button>
        </div>
      </header>

      <div className="net__body">
        <div className="net__canvas" ref={wrapRef}>
          <canvas
            ref={canvasRef}
            role="img"
            aria-label={`Orthographic view of ${elements.length} satellites and ${links} inter-satellite links. Drag to rotate.`}
            onPointerDown={onPointerDown}
            onPointerMove={onPointerMove}
            onPointerUp={onPointerUp}
            onPointerCancel={onPointerUp}
          />
          <ul className="net__legend" aria-label="Legend">
            <li><span className="net__swatch net__swatch--node" /> Satellite</li>
            <li><span className="net__swatch net__swatch--link" /> Inter-satellite link</li>
            <li><span className="net__swatch net__swatch--explored" /> Explored by {shown === 'ASTAR' ? 'A*' : 'Dijkstra'}</li>
            <li><span className="net__swatch net__swatch--route" /> Rescue route</li>
            <li><span className="net__swatch net__swatch--source" /> Rescuer · <span className="net__swatch net__swatch--target" /> Casualty</li>
          </ul>
        </div>

        <dl className="net__stats">
          <div className="net__group">Graph</div>
          <div><dt>Nodes</dt><dd>{elements.length.toLocaleString()}</dd></div>
          <div><dt>Links</dt><dd>{links.toLocaleString()}</dd></div>
          <div><dt>Mean degree</dt><dd>{meanDegree.toFixed(2)}</dd></div>
          <div>
            <dt>Components</dt>
            <dd>
              {frame?.componentCount ?? '—'}
              {frame ? (
                <span className="net__aside">
                  {' '}largest {Math.round((frame.largestComponent / Math.max(1, elements.length)) * 100)}%
                </span>
              ) : null}
            </dd>
          </div>
          <div><dt>Grid cells</dt><dd>{frame?.graph.gridCells.toLocaleString() ?? '—'}</dd></div>
          <div><dt>Pairs tested</dt><dd>{frame?.graph.candidatePairsTested.toLocaleString() ?? '—'}</dd></div>
          <div><dt>Blocked by Earth</dt><dd>{frame?.graph.occludedPairs.toLocaleString() ?? '—'}</dd></div>
          <div><dt>Build time</dt><dd>{frame ? `${frame.graph.buildMs.toFixed(1)} ms` : '—'}</dd></div>

          <div className="net__group">Route</div>
          {result && !result.found ? (
            <p className="net__warn">
              No route: rescuer and casualty are in different components. Raise the link range or the
              links per satellite.
            </p>
          ) : null}
          <div><dt>Hops</dt><dd>{result?.found ? result.path.length - 1 : '—'}</dd></div>
          <div><dt>Length</dt><dd>{result?.found ? `${Math.round(result.lengthKm).toLocaleString()} km` : '—'}</dd></div>

          <div className="net__group">Dijkstra vs A*</div>
          <div className="net__compare">
            <span />
            <span>Dijkstra</span>
            <span>A*</span>
            <span>Expanded</span>
            <span>{frame?.dijkstra?.nodesExpanded.toLocaleString() ?? '—'}</span>
            <span>{frame?.aStar?.nodesExpanded.toLocaleString() ?? '—'}</span>
            <span>Relaxed</span>
            <span>{frame?.dijkstra?.edgesRelaxed.toLocaleString() ?? '—'}</span>
            <span>{frame?.aStar?.edgesRelaxed.toLocaleString() ?? '—'}</span>
            <span>Heap pushes</span>
            <span>{frame?.dijkstra?.heapPushes.toLocaleString() ?? '—'}</span>
            <span>{frame?.aStar?.heapPushes.toLocaleString() ?? '—'}</span>
            <span>Time</span>
            <span>{frame?.dijkstra ? `${frame.dijkstra.ms.toFixed(2)} ms` : '—'}</span>
            <span>{frame?.aStar ? `${frame.aStar.ms.toFixed(2)} ms` : '—'}</span>
          </div>
          {saving !== null ? (
            <p className="net__note">
              A* settled <strong>{Math.round(saving * 100)}% fewer</strong> nodes for the same
              {frame?.dijkstra && frame.aStar && Math.abs(frame.dijkstra.lengthKm - frame.aStar.lengthKm) < 1e-6
                ? ' optimal route — identical length, as an admissible heuristic guarantees.'
                : ' route.'}
            </p>
          ) : null}
        </dl>
      </div>
    </section>
  );
}

/* ======================================================================== render ====== */

function paint(
  canvas: HTMLCanvasElement,
  wrap: HTMLDivElement,
  positions: readonly Vec3[],
  frame: Frame,
  shown: 'DIJKSTRA' | 'ASTAR',
  source: number,
  target: number,
  yaw: number,
  pitch: number,
): void {
  const dpr = Math.min(2, typeof window === 'undefined' ? 1 : window.devicePixelRatio || 1);
  const width = wrap.clientWidth;
  const height = wrap.clientHeight;
  if (width === 0 || height === 0) return;
  if (canvas.width !== Math.round(width * dpr) || canvas.height !== Math.round(height * dpr)) {
    canvas.width = Math.round(width * dpr);
    canvas.height = Math.round(height * dpr);
    canvas.style.width = `${width}px`;
    canvas.style.height = `${height}px`;
  }

  const ctx = canvas.getContext('2d');
  if (!ctx) return;
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, width, height);

  const cx = width / 2;
  const cy = height / 2;
  const scale = (Math.min(width, height) * 0.46) / (EARTH_RADIUS_KM + 1_350);

  const cosY = Math.cos(yaw);
  const sinY = Math.sin(yaw);
  const cosP = Math.cos(pitch);
  const sinP = Math.sin(pitch);

  // Yaw about the polar axis, then pitch about screen-x. Screen y is the rotated z (north up);
  // `depth` is toward the viewer.
  const sx = new Float32Array(positions.length);
  const sy = new Float32Array(positions.length);
  const visible = new Uint8Array(positions.length);
  const front = new Uint8Array(positions.length);
  const earthR = EARTH_RADIUS_KM * scale;

  positions.forEach((p, i) => {
    const x1 = p.x * cosY - p.y * sinY;
    const y1 = p.x * sinY + p.y * cosY;
    const depth = y1 * cosP - p.z * sinP;
    const z2 = y1 * sinP + p.z * cosP;
    sx[i] = cx + x1 * scale;
    sy[i] = cy - z2 * scale;
    const r = Math.hypot(sx[i]! - cx, sy[i]! - cy);
    front[i] = depth >= 0 ? 1 : 0;
    // Behind the planet only when both further away and inside the disc.
    visible[i] = depth >= 0 || r > earthR ? 1 : 0;
  });

  const colour = {
    planet: readToken('--orca-surface-elevated', '#121a26'),
    limb: readToken('--orca-primary', '#4a90d6'),
    node: readToken('--orca-muted', '#7f8fa6'),
    link: readToken('--orca-primary', '#4a90d6'),
    explored: readToken('--orca-band-meo', '#a878ff'),
    route: readToken('--orca-warning', '#d6a344'),
    source: readToken('--orca-success', '#45b787'),
    target: readToken('--orca-critical', '#d6544a'),
  };

  // Planet: a filled disc with a soft limb glow.
  const glow = ctx.createRadialGradient(cx, cy, earthR * 0.92, cx, cy, earthR * 1.18);
  glow.addColorStop(0, `${colour.limb}55`);
  glow.addColorStop(1, `${colour.limb}00`);
  ctx.fillStyle = glow;
  ctx.beginPath();
  ctx.arc(cx, cy, earthR * 1.18, 0, Math.PI * 2);
  ctx.fill();

  ctx.fillStyle = colour.planet;
  ctx.beginPath();
  ctx.arc(cx, cy, earthR, 0, Math.PI * 2);
  ctx.fill();
  // Terminator-style shading: a highlight toward the upper left, so the disc reads as a sphere.
  const shade = ctx.createRadialGradient(cx - earthR * 0.4, cy - earthR * 0.4, earthR * 0.05, cx, cy, earthR);
  shade.addColorStop(0, `${colour.limb}30`);
  shade.addColorStop(1, `${colour.limb}00`);
  ctx.fillStyle = shade;
  ctx.fill();
  ctx.strokeStyle = `${colour.limb}66`;
  ctx.lineWidth = 1;
  ctx.stroke();

  // Graticule: a few meridians and parallels, so rotation reads as rotation.
  ctx.strokeStyle = `${colour.limb}1f`;
  ctx.lineWidth = 0.8;
  for (let lat = -60; lat <= 60; lat += 30) {
    drawCircle(ctx, (t) => {
      const la = (lat * Math.PI) / 180;
      return [Math.cos(la) * Math.cos(t), Math.cos(la) * Math.sin(t), Math.sin(la)];
    });
  }
  for (let lon = 0; lon < 180; lon += 30) {
    drawCircle(ctx, (t) => {
      const lo = (lon * Math.PI) / 180;
      return [Math.cos(t) * Math.cos(lo), Math.cos(t) * Math.sin(lo), Math.sin(t)];
    });
  }

  function drawCircle(c: CanvasRenderingContext2D, at: (t: number) => [number, number, number]): void {
    c.beginPath();
    let pen = false;
    for (let k = 0; k <= 72; k += 1) {
      const [x, y, z] = at((k / 72) * Math.PI * 2);
      const x1 = x * cosY - y * sinY;
      const y1 = x * sinY + y * cosY;
      const depth = y1 * cosP - z * sinP;
      const z2 = y1 * sinP + z * cosP;
      if (depth < 0) {
        pen = false;
        continue;
      }
      const px = cx + x1 * earthR;
      const py = cy - z2 * earthR;
      if (pen) c.lineTo(px, py);
      else c.moveTo(px, py);
      pen = true;
    }
    c.stroke();
  }

  const result = shown === 'ASTAR' ? frame.aStar : frame.dijkstra;

  // Links: one path, one stroke — thousands of segments in a single draw call.
  ctx.strokeStyle = `${colour.link}40`;
  ctx.lineWidth = 0.7;
  ctx.beginPath();
  for (const { a, b } of frame.graph.links) {
    if (!visible[a] || !visible[b]) continue;
    ctx.moveTo(sx[a]!, sy[a]!);
    ctx.lineTo(sx[b]!, sy[b]!);
  }
  ctx.stroke();

  // Nodes: back hemisphere dimmer than front, explored region tinted.
  for (let i = 0; i < positions.length; i += 1) {
    if (!visible[i]) continue;
    const explored = result?.settled[i] === 1;
    ctx.fillStyle = explored ? colour.explored : colour.node;
    ctx.globalAlpha = front[i] ? (explored ? 0.95 : 0.8) : 0.35;
    const r = explored ? 1.9 : 1.3;
    ctx.fillRect(sx[i]! - r, sy[i]! - r, r * 2, r * 2);
  }
  ctx.globalAlpha = 1;

  // Route on top.
  if (result?.found && result.path.length > 1) {
    ctx.strokeStyle = colour.route;
    ctx.lineWidth = 2.6;
    ctx.lineJoin = 'round';
    ctx.shadowColor = colour.route;
    ctx.shadowBlur = 8;
    ctx.beginPath();
    result.path.forEach((node, k) => {
      if (k === 0) ctx.moveTo(sx[node]!, sy[node]!);
      else ctx.lineTo(sx[node]!, sy[node]!);
    });
    ctx.stroke();
    ctx.shadowBlur = 0;
  }

  for (const [node, fill] of [
    [source, colour.source],
    [target, colour.target],
  ] as const) {
    ctx.fillStyle = fill;
    ctx.strokeStyle = fill;
    ctx.globalAlpha = visible[node] ? 1 : 0.45;
    ctx.beginPath();
    ctx.arc(sx[node]!, sy[node]!, 5, 0, Math.PI * 2);
    ctx.fill();
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    ctx.arc(sx[node]!, sy[node]!, 10, 0, Math.PI * 2);
    ctx.stroke();
  }
  ctx.globalAlpha = 1;
}

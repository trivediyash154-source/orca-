import { CanvasTexture, LinearFilter, RepeatWrapping, SRGBColorSpace } from 'three';

import { LANDMASSES, type LonLat } from './continents';

/** Deterministic PRNG (mulberry32) — NOT `Math.random()`, so the generated textures look
 * identical every reload rather than flickering between dev sessions. Presentation-only:
 * nothing here is ever read as simulation data. */
function mulberry32(seed: number): () => number {
  let a = seed;
  return () => {
    a |= 0;
    a = (a + 0x6d2b79f5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

const WIDTH = 2048;
const HEIGHT = 1024;

/** Equirectangular projection: longitude -180..180 -> x 0..W, latitude 90..-90 -> y 0..H.
 * The texture's u=0 seam sits at longitude -180, which is what `Earth.tsx`'s sphere UV
 * layout expects. */
function project(lon: number, lat: number): [number, number] {
  return [((lon + 180) / 360) * WIDTH, ((90 - lat) / 180) * HEIGHT];
}

function tracePolygon(ctx: CanvasRenderingContext2D, poly: readonly LonLat[], offsetX = 0): void {
  ctx.beginPath();
  poly.forEach(([lon, lat], i) => {
    const [x, y] = project(lon, lat);
    if (i === 0) ctx.moveTo(x + offsetX, y);
    else ctx.lineTo(x + offsetX, y);
  });
  ctx.closePath();
}

/** Paints the Earth day map: ocean depth gradient, real (coarse) continent shapes from
 * `continents.ts`, terrain colour variation, polar ice. */
function paintEarthCanvas(): HTMLCanvasElement {
  const canvas = document.createElement('canvas');
  canvas.width = WIDTH;
  canvas.height = HEIGHT;
  const ctx = canvas.getContext('2d');
  if (!ctx) return canvas;
  const rand = mulberry32(0x0cea17);

  // --- Ocean -------------------------------------------------------------------
  const ocean = ctx.createLinearGradient(0, 0, 0, HEIGHT);
  ocean.addColorStop(0.0, '#0b2f52');
  ocean.addColorStop(0.2, '#10456f');
  ocean.addColorStop(0.5, '#12639b');
  ocean.addColorStop(0.8, '#10456f');
  ocean.addColorStop(1.0, '#0b2f52');
  ctx.fillStyle = ocean;
  ctx.fillRect(0, 0, WIDTH, HEIGHT);

  // Subtle abyssal mottling so the sea is not a flat wash.
  ctx.globalAlpha = 0.05;
  for (let i = 0; i < 260; i++) {
    const x = rand() * WIDTH;
    const y = rand() * HEIGHT;
    const r = 40 + rand() * 160;
    const g = ctx.createRadialGradient(x, y, 0, x, y, r);
    g.addColorStop(0, rand() > 0.5 ? '#0a2a48' : '#1a7fc0');
    g.addColorStop(1, 'transparent');
    ctx.fillStyle = g;
    ctx.fillRect(x - r, y - r, r * 2, r * 2);
  }
  ctx.globalAlpha = 1;

  // --- Landmasses ---------------------------------------------------------------
  // Drawn three times (at -W, 0, +W) so a polygon crossing the antimeridian still wraps
  // correctly rather than clipping at the seam.
  for (const offsetX of [-WIDTH, 0, WIDTH]) {
    for (const poly of LANDMASSES) {
      tracePolygon(ctx, poly, offsetX);
      ctx.fillStyle = '#3f6b3a';
      ctx.fill();
    }
  }

  // Terrain variation, clipped to land only: deserts, forest, highlands. Clipping means the
  // variation never bleeds into the ocean, which is what made the earlier blob-based
  // attempt read as noise.
  ctx.save();
  ctx.beginPath();
  for (const offsetX of [-WIDTH, 0, WIDTH]) {
    for (const poly of LANDMASSES) {
      poly.forEach(([lon, lat], i) => {
        const [x, y] = project(lon, lat);
        if (i === 0) ctx.moveTo(x + offsetX, y);
        else ctx.lineTo(x + offsetX, y);
      });
      ctx.closePath();
    }
  }
  ctx.clip();

  const terrain = ['#6b6a3a', '#8a7b4a', '#4e7340', '#35592e', '#7d8449', '#2f5233'];
  for (let i = 0; i < 900; i++) {
    const x = rand() * WIDTH;
    const y = rand() * HEIGHT;
    const r = 18 + rand() * 90;
    const g = ctx.createRadialGradient(x, y, 0, x, y, r);
    g.addColorStop(0, terrain[Math.floor(rand() * terrain.length)]!);
    g.addColorStop(1, 'transparent');
    ctx.globalAlpha = 0.5;
    ctx.fillStyle = g;
    ctx.fillRect(x - r, y - r, r * 2, r * 2);
  }
  // Sahara / Arabian / Australian desert belts, still land-clipped.
  ctx.globalAlpha = 0.42;
  for (const [lon, lat, rx, ry] of [
    [15, 22, 300, 110],
    [45, 22, 150, 90],
    [72, 27, 110, 60],
    [132, -25, 190, 100],
    [-8, 28, 130, 70],
    [105, 40, 160, 70],
  ] as const) {
    const [cx, cy] = project(lon, lat);
    const g = ctx.createRadialGradient(cx, cy, 0, cx, cy, Math.max(rx, ry));
    g.addColorStop(0, '#c2a86a');
    g.addColorStop(0.6, '#a8914f');
    g.addColorStop(1, 'transparent');
    ctx.fillStyle = g;
    ctx.save();
    ctx.translate(cx, cy);
    ctx.scale(1, ry / rx);
    ctx.beginPath();
    ctx.arc(0, 0, rx, 0, Math.PI * 2);
    ctx.fill();
    ctx.restore();
  }
  ctx.globalAlpha = 1;
  ctx.restore();

  // --- Polar ice ----------------------------------------------------------------
  const north = ctx.createLinearGradient(0, 0, 0, HEIGHT * 0.13);
  north.addColorStop(0, 'rgba(238,246,252,0.95)');
  north.addColorStop(1, 'rgba(238,246,252,0)');
  ctx.fillStyle = north;
  ctx.fillRect(0, 0, WIDTH, HEIGHT * 0.13);

  const south = ctx.createLinearGradient(0, HEIGHT * 0.85, 0, HEIGHT);
  south.addColorStop(0, 'rgba(238,246,252,0)');
  south.addColorStop(1, 'rgba(238,246,252,0.98)');
  ctx.fillStyle = south;
  ctx.fillRect(0, HEIGHT * 0.85, WIDTH, HEIGHT * 0.15);

  return canvas;
}

/** A separate transparent cloud map, rendered on its own slightly larger sphere that
 * rotates at a different rate than the surface (`Earth.tsx`) -- the cheapest treatment that
 * reads as weather rather than a static decal. */
function paintCloudCanvas(): HTMLCanvasElement {
  const canvas = document.createElement('canvas');
  canvas.width = WIDTH;
  canvas.height = HEIGHT;
  const ctx = canvas.getContext('2d');
  if (!ctx) return canvas;
  const rand = mulberry32(0xc10d5);

  ctx.clearRect(0, 0, WIDTH, HEIGHT);
  ctx.fillStyle = '#ffffff';

  // Banded distribution: heavier near the equator and mid-latitudes, thin at the poles --
  // a coarse nod to real circulation cells, not a weather model.
  for (let i = 0; i < 900; i++) {
    const band = rand();
    const lat = band < 0.35 ? (rand() - 0.5) * 24 : band < 0.75 ? (rand() < 0.5 ? -1 : 1) * (35 + rand() * 25) : (rand() - 0.5) * 150;
    const lon = rand() * 360 - 180;
    const [cx, cy] = project(lon, lat);
    const rx = 30 + rand() * 130;
    const ry = rx * (0.25 + rand() * 0.3);
    const g = ctx.createRadialGradient(cx, cy, 0, cx, cy, rx);
    g.addColorStop(0, 'rgba(255,255,255,0.85)');
    g.addColorStop(0.5, 'rgba(255,255,255,0.35)');
    g.addColorStop(1, 'rgba(255,255,255,0)');
    ctx.fillStyle = g;
    ctx.save();
    ctx.translate(cx, cy);
    ctx.scale(1, ry / rx);
    ctx.beginPath();
    ctx.arc(0, 0, rx, 0, Math.PI * 2);
    ctx.fill();
    ctx.restore();
  }
  return canvas;
}

let cachedDay: CanvasTexture | null = null;
let cachedClouds: CanvasTexture | null = null;

function finish(canvas: HTMLCanvasElement): CanvasTexture {
  const tex = new CanvasTexture(canvas);
  tex.colorSpace = SRGBColorSpace;
  tex.wrapS = RepeatWrapping;
  tex.minFilter = LinearFilter;
  tex.magFilter = LinearFilter;
  tex.needsUpdate = true;
  return tex;
}

/** Memoized -- painting is a one-time cost regardless of how many `<Earth>` instances mount. */
export function getEarthDayTexture(): CanvasTexture {
  if (!cachedDay) cachedDay = finish(paintEarthCanvas());
  return cachedDay;
}

export function getEarthCloudTexture(): CanvasTexture {
  if (!cachedClouds) cachedClouds = finish(paintCloudCanvas());
  return cachedClouds;
}

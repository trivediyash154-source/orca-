import { CanvasTexture } from 'three';

/** A soft radial-gradient sprite, white-on-transparent, tinted per-instance via
 * `THREE.Points`' own vertex colors (`PointsMaterial.vertexColors` multiplies this texture's
 * RGB by each point's color) -- one shared texture for every marker regardless of status or
 * kind. Generated once, cached, no external asset (matches `earthTexture.ts`'s own reasoning
 * for why this project paints its own decoration rather than fetching one). */
let cachedGlow: CanvasTexture | null = null;

export function getMarkerGlowTexture(): CanvasTexture {
  if (cachedGlow) return cachedGlow;
  const size = 64;
  const canvas = document.createElement('canvas');
  canvas.width = size;
  canvas.height = size;
  const ctx = canvas.getContext('2d')!;
  const g = ctx.createRadialGradient(size / 2, size / 2, 0, size / 2, size / 2, size / 2);
  g.addColorStop(0, 'rgba(255,255,255,1)');
  g.addColorStop(0.35, 'rgba(255,255,255,0.85)');
  g.addColorStop(0.7, 'rgba(255,255,255,0.25)');
  g.addColorStop(1, 'rgba(255,255,255,0)');
  ctx.fillStyle = g;
  ctx.fillRect(0, 0, size, size);
  cachedGlow = new CanvasTexture(canvas);
  cachedGlow.needsUpdate = true;
  return cachedGlow;
}

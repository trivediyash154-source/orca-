import { Html } from '@react-three/drei';
import { useFrame } from '@react-three/fiber';
import { useRef } from 'react';

/**
 * Text tags pinned to the ends of each live rescue: the rescuer, and the rendezvous point it is
 * flying to. Without them a transfer arc read as a stray curve around the planet -- nothing on
 * screen said which vehicle it belonged to or where it ended.
 *
 * A tag whose point is behind the Earth is faded out each frame, by a ray-sphere test against
 * the unit-radius planet, rather than left floating over the far side of the globe.
 */

export interface MissionTag {
  readonly id: string;
  readonly position: readonly [number, number, number];
  readonly text: string;
  readonly tone: 'vehicle' | 'target';
}

/** Whether the segment from the camera to `p` passes through the unit sphere. */
function behindEarth(p: readonly [number, number, number], cam: { x: number; y: number; z: number }): boolean {
  const dx = p[0] - cam.x;
  const dy = p[1] - cam.y;
  const dz = p[2] - cam.z;
  const dd = dx * dx + dy * dy + dz * dz;
  if (dd < 1e-12) return false;
  const t = -(cam.x * dx + cam.y * dy + cam.z * dz) / dd;
  if (t <= 0 || t >= 1) return false;
  const cx = cam.x + t * dx;
  const cy = cam.y + t * dy;
  const cz = cam.z + t * dz;
  return cx * cx + cy * cy + cz * cz < 0.995;
}

export function MissionTags({ tags }: { readonly tags: readonly MissionTag[] }) {
  const elements = useRef(new Map<string, HTMLDivElement>());

  useFrame(({ camera }) => {
    for (const tag of tags) {
      const element = elements.current.get(tag.id);
      if (element) element.style.opacity = behindEarth(tag.position, camera.position) ? '0' : '1';
    }
  });

  return (
    <>
      {tags.map((tag) => (
        <Html
          key={tag.id}
          position={tag.position as [number, number, number]}
          zIndexRange={[4, 0]}
          style={{ pointerEvents: 'none' }}
        >
          <div
            ref={(node) => {
              if (node) elements.current.set(tag.id, node);
              else elements.current.delete(tag.id);
            }}
            className={`scene-mission-tag scene-mission-tag--${tag.tone}`}
          >
            {tag.text}
          </div>
        </Html>
      ))}
    </>
  );
}

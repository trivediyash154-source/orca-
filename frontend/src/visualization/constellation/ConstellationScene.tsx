import { OrbitControls, Stars } from '@react-three/drei';
import { Canvas } from '@react-three/fiber';

import { Earth } from '../earth/Earth';
import { LargeConstellationField } from './LargeConstellationField';
import type { VisualObjectView } from '../../types/engine';

export interface ConstellationSceneProps {
  readonly satellites: readonly VisualObjectView[];
  readonly vehicles: readonly VisualObjectView[];
  readonly selectedId: string | null;
  readonly onSelect: (id: string) => void;
  readonly proximityMode: boolean;
}

/** Phase 12 brief §5's large-scale visual constellation scene -- a separate `<Canvas>` from
 * Mission Control's `Scene.tsx` (that one stays tied to the live, small mission-narrative
 * session; this one renders whatever `GET /visual/constellation` last returned). Orbit rings
 * are intentionally NOT drawn here: at thousands of objects, thousands of rings would be
 * pure visual noise, not structure (brief §7's own "do not create one confusing pile of
 * rings" warning) -- proximity coloring (`LargeConstellationField`) carries the spatial-
 * structure job instead. */
export function ConstellationScene({
  satellites,
  vehicles,
  selectedId,
  onSelect,
  proximityMode,
}: ConstellationSceneProps) {
  return (
    <Canvas camera={{ position: [0, 1.4, 3.4], fov: 50 }} style={{ background: '#05070d' }}>
      <fog attach="fog" args={['#05070d', 3, 20]} />
      <ambientLight intensity={0.55} />
      <directionalLight position={[5, 3, 2]} intensity={1.6} />
      <pointLight position={[-4, -2, -3]} intensity={0.25} color="#4fa8ff" />
      <Stars radius={100} depth={50} count={4000} factor={2} fade />
      <Earth />

      <LargeConstellationField
        items={satellites}
        pointSize={0.022}
        selectedId={selectedId}
        onSelect={onSelect}
        proximityMode={proximityMode}
      />
      <LargeConstellationField
        items={vehicles}
        pointSize={0.03}
        selectedId={selectedId}
        onSelect={onSelect}
        proximityMode={proximityMode}
      />

      <OrbitControls enablePan minDistance={1.15} maxDistance={18} />
    </Canvas>
  );
}

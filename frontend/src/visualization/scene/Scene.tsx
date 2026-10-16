import { OrbitControls, Stars } from '@react-three/drei';
import { Canvas } from '@react-three/fiber';
import { useCallback, useMemo, useRef } from 'react';
import type { OrbitControls as OrbitControlsImpl } from 'three-stdlib';
import { Vector3 } from 'three';

import { OrcaEarth } from '../earth/OrcaEarth';
import { OrbitRing } from '../orbit/OrbitRing';
import { EntityMarkerField } from '../satellites/EntityMarkerField';
import { MissionLink } from '../trajectories/MissionLink';
import { ConstellationLayer } from '../constellation/ConstellationLayer';
import { useVisualConstellation } from '../constellation/useVisualConstellation';
import { eciToSceneExaggerated } from '../coordinates';
import { useEngine } from '../../state/EngineContext';
import { useUI } from '../../state/UIContext';
import { useTheme } from '../../state/ThemeContext';
import type { VisualObjectView } from '../../types/engine';

export type CameraFocus = 'earth' | 'selected' | 'mission';

export interface SceneProps {
  /** Bumping this number re-runs the camera framing action named by `focusTarget` --
   * a plain counter rather than a callback ref so the parent (`MissionControl`) can trigger
   * focus without holding a handle on the r3f-internal controls object. */
  readonly focusNonce?: number;
  readonly focusTarget?: CameraFocus;
  readonly onConstellationSelect?: (obj: VisualObjectView | null) => void;
  /** The currently-selected SYNTHETIC constellation object, if any. Kept separate from
   * `EngineContext`'s `selected` (which addresses REAL planning entities) so the two
   * selection domains never alias each other -- but it must reach `ConstellationLayer`, or
   * the highlight and proximity anchor silently do nothing (a real bug caught in browser). */
  readonly syntheticSelectedId?: string | null;
}

const DEFAULT_CAMERA = new Vector3(0, 1.6, 4.2);

/** Applies a camera framing action. Kept inside the Canvas because it needs the live
 * `OrbitControls` instance; driven by a nonce from outside (see `SceneProps`). */
function CameraDirector({
  controls,
  nonce,
  target,
  selectedPos,
  missionPos,
}: {
  controls: React.RefObject<OrbitControlsImpl>;
  nonce: number;
  target: CameraFocus;
  selectedPos: [number, number, number] | null;
  missionPos: [number, number, number] | null;
}) {
  const lastNonce = useRef(0);
  if (nonce !== lastNonce.current && controls.current) {
    lastNonce.current = nonce;
    const c = controls.current;
    if (target === 'earth') {
      c.target.set(0, 0, 0);
      c.object.position.copy(DEFAULT_CAMERA);
    } else {
      const p = target === 'selected' ? selectedPos : missionPos;
      if (p) {
        const v = new Vector3(p[0], p[1], p[2]);
        c.target.copy(v);
        // Stand off along the object's own radial direction so Earth stays behind it.
        c.object.position.copy(v.clone().multiplyScalar(1.55).add(new Vector3(0, 0.25, 0)));
      }
    }
    c.update();
  }
  return null;
}

/** Mission Control's orbital visualization. Three distinct layers, deliberately separate
 * (Phase 12 brief §2):
 *
 *   A. SYNTHETIC VISUAL CONSTELLATION -- thousands of generated objects (`ConstellationLayer`),
 *      never part of the planning graph;
 *   B. REAL PLANNING ENTITIES -- the engine's own satellites/vehicles (`EntityMarkerField`),
 *      which ARE the computational network;
 *   C. ACTIVE MISSION TRAJECTORY -- the strongest visual element (`MissionLink`).
 *
 * Positions of A and B both pass through the documented altitude-exaggeration transform
 * (`coordinates.ts`) so shells are distinguishable; every real distance/metric the UI
 * REPORTS uses untransformed kilometres. */
export function Scene({
  focusNonce = 0,
  focusTarget = 'earth',
  onConstellationSelect,
  syntheticSelectedId = null,
}: SceneProps) {
  const { snapshot, selected, select } = useEngine();
  const { theme } = useTheme();
  const {
    showClouds,
    showAtmosphere,
    showOrbitRings,
    showStarfield,
    showTrajectories,
    showConstellationRibbons,
    proximityMode,
    altitudeExaggeration,
    performanceMode,
  } = useUI();
  const constellation = useVisualConstellation();
  const controlsRef = useRef<OrbitControlsImpl>(null);

  /**
   * Mission Control's Earth turns on ENGINE time, not wall-clock time, so the globe and the
   * engine-derived entity positions agree. The engine reports `simulationTimeSec` from zero, and
   * a terminator needs a real date, so t=0 is anchored to the instant this scene mounted: the
   * sun direction is then correct for today and advances as the simulation runs.
   *
   * Both are refs read inside the render loop rather than state: the Earth re-reads them every
   * frame, and putting simulation time into React state would re-render this whole subtree on
   * every snapshot.
   */
  const mountedAtMs = useRef(Date.now());
  const simTimeSecRef = useRef(0);
  simTimeSecRef.current = snapshot?.simulationTimeSec ?? 0;
  const getEpochMs = useCallback(
    () => mountedAtMs.current + simTimeSecRef.current * 1000,
    [],
  );

  const satelliteItems = useMemo(
    () =>
      snapshot?.satellites.map((sat) => ({
        id: sat.id,
        label: sat.name,
        status: sat.status,
        position: sat.position,
        altitudeKm: sat.orbit.altitudeKm,
        fuelFraction: sat.fuelFraction,
        health: sat.health,
      })) ?? [],
    [snapshot],
  );

  const vehicleItems = useMemo(
    () =>
      snapshot?.rescueVehicles.map((v) => ({
        id: v.id,
        label: v.name,
        status: v.status,
        position: v.position,
        altitudeKm: v.orbit.altitudeKm,
        fuelFraction: v.fuelFraction,
        health: v.health,
        deltaVRemainingKms: v.deltaVRemainingKms,
      })) ?? [],
    [snapshot],
  );

  const selectedPos = useMemo<[number, number, number] | null>(() => {
    if (!selected || !snapshot) return null;
    const real =
      selected.kind === 'satellite'
        ? snapshot.satellites.find((s) => s.id === selected.id)
        : selected.kind === 'vehicle'
          ? snapshot.rescueVehicles.find((v) => v.id === selected.id)
          : undefined;
    if (real) return eciToSceneExaggerated(real.position, altitudeExaggeration);
    return null;
  }, [selected, snapshot, altitudeExaggeration]);

  const syntheticPos = useMemo<[number, number, number] | null>(() => {
    if (!syntheticSelectedId || !constellation.data) return null;
    const o = [...constellation.data.satellites, ...constellation.data.vehicles].find(
      (x) => x.id === syntheticSelectedId,
    );
    return o ? eciToSceneExaggerated(o.position, altitudeExaggeration) : null;
  }, [syntheticSelectedId, constellation.data, altitudeExaggeration]);

  const missionPos = useMemo<[number, number, number] | null>(() => {
    const leg = snapshot?.missions.find((m) => m.state === 'IN_TRANSIT')?.legs.find((l) => l.status === 'current');
    return leg ? eciToSceneExaggerated(leg.toPosition, altitudeExaggeration) : null;
  }, [snapshot, altitudeExaggeration]);

  const handleConstellationSelect = (id: string) => {
    const obj = [...(constellation.data?.satellites ?? []), ...(constellation.data?.vehicles ?? [])].find(
      (o) => o.id === id,
    );
    onConstellationSelect?.(obj ?? null);
    select(null);
  };

  return (
    <Canvas camera={{ position: [0, 1.6, 4.2], fov: 50 }} style={{ background: '#04060c' }}>
      <fog attach="fog" args={['#04060c', 6, 30]} />
      <ambientLight intensity={0.42} />
      {/* Key light -- gives Earth a real terminator (day/night division) rather than flat
          uniform illumination. */}
      <directionalLight position={[6, 2.5, 3]} intensity={2.1} />
      <pointLight position={[-5, -2, -4]} intensity={0.3} color="#4fa8ff" />
      {showStarfield && <Stars radius={120} depth={60} count={6000} factor={3} fade />}
      {/* The same component the Orbital Catalog uses -- real NASA imagery, a real terminator
          and night-side city lights. Mission Control previously mounted a second, weaker Earth
          that painted its coastlines at runtime; there is now one planet in the product. */}
      <OrcaEarth
        getEpochMs={getEpochMs}
        theme={theme}
        showClouds={showClouds}
        showAtmosphere={showAtmosphere}
      />

      {constellation.data && (
        <ConstellationLayer
          satellites={constellation.data.satellites}
          vehicles={constellation.data.vehicles}
          selectedId={syntheticSelectedId}
          onSelect={handleConstellationSelect}
          proximityMode={proximityMode}
          exaggeration={altitudeExaggeration}
          showRibbons={showConstellationRibbons}
          performanceMode={performanceMode}
        />
      )}

      {snapshot && (
        <>
          {showOrbitRings &&
            snapshot.satellites.map((sat) => (
              <OrbitRing key={`orbit-sat-${sat.id}`} orbit={sat.orbit} exaggeration={altitudeExaggeration} />
            ))}
          {showOrbitRings &&
            snapshot.rescueVehicles.map((v) => (
              <OrbitRing key={`orbit-veh-${v.id}`} orbit={v.orbit} exaggeration={altitudeExaggeration} />
            ))}

          <EntityMarkerField
            items={satelliteItems}
            kind="satellite"
            selectedId={selected?.kind === 'satellite' ? selected.id : null}
            onSelect={(id) => select({ kind: 'satellite', id })}
            exaggeration={altitudeExaggeration}
          />
          <EntityMarkerField
            items={vehicleItems}
            kind="vehicle"
            selectedId={selected?.kind === 'vehicle' ? selected.id : null}
            onSelect={(id) => select({ kind: 'vehicle', id })}
            exaggeration={altitudeExaggeration}
          />

          {showTrajectories &&
            snapshot.missions.flatMap((mission) =>
              mission.legs.map((leg, i) => (
                <MissionLink key={`${mission.id}-leg-${i}`} leg={leg} exaggeration={altitudeExaggeration} />
              )),
            )}
        </>
      )}

      <CameraDirector
        controls={controlsRef}
        nonce={focusNonce}
        target={focusTarget}
        selectedPos={selectedPos ?? syntheticPos}
        missionPos={missionPos}
      />
      <OrbitControls ref={controlsRef} enablePan minDistance={1.2} maxDistance={30} />
    </Canvas>
  );
}

import { OrbitControls, Stars } from '@react-three/drei';
import { Canvas } from '@react-three/fiber';
import { useCallback, useMemo, useRef } from 'react';
import type { OrbitControls as OrbitControlsImpl } from 'three-stdlib';
import { Vector3 } from 'three';

import { OrcaEarth } from '../earth/OrcaEarth';
import { OrbitRing } from '../orbit/OrbitRing';
import { EntityMarkerField } from '../satellites/EntityMarkerField';
import { MissionLink } from '../trajectories/MissionLink';
import { MissionTags, type MissionTag } from '../trajectories/MissionTags';
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

/** Where to look from, and at, to show a mission leg. */
interface MissionFrame {
  readonly target: Vector3;
  readonly camera: Vector3;
}

/** States in which a mission still has a leg worth framing (the engine's `MissionState`). */
const LIVE_MISSION_STATES = new Set(['LAUNCHED', 'IN_TRANSIT', 'REPLANNING', 'RENDEZVOUS']);

/**
 * Frames a transfer leg so it reads as the curve it is.
 *
 * The previous framing stood the camera on the leg endpoint's radial line. That line lies in the
 * transfer's own orbital plane (every great-circle plane contains Earth's centre), so the arc was
 * always seen exactly edge-on and drawn as a straight stroke through the planet: the single most
 * "broken"-looking thing in Mission Control. The camera now sits 55 degrees out of that plane,
 * looking at a point between the planet and the arc's midpoint, far enough back to hold both
 * endpoints and the limb in view.
 */
function frameLeg(from: [number, number, number], to: [number, number, number]): MissionFrame {
  const a = new Vector3(...from);
  const b = new Vector3(...to);
  const radius = Math.max(a.length(), b.length());
  const mid = a.clone().add(b);
  if (mid.lengthSq() < 1e-9) mid.set(0, 1, 0);
  mid.normalize();

  const normal = a.clone().cross(b);
  if (normal.lengthSq() < 1e-9) normal.set(0, 1, 0);
  normal.normalize();

  const tilt = (55 * Math.PI) / 180;
  const direction = mid.clone().multiplyScalar(Math.cos(tilt)).add(normal.multiplyScalar(Math.sin(tilt))).normalize();

  return {
    target: mid.clone().multiplyScalar(radius * 0.45),
    camera: direction.multiplyScalar(radius * 2.5),
  };
}

/** Applies a camera framing action. Kept inside the Canvas because it needs the live
 * `OrbitControls` instance; driven by a nonce from outside (see `SceneProps`). */
function CameraDirector({
  controls,
  nonce,
  target,
  selectedPos,
  missionFrame,
}: {
  controls: React.RefObject<OrbitControlsImpl>;
  nonce: number;
  target: CameraFocus;
  selectedPos: [number, number, number] | null;
  missionFrame: MissionFrame | null;
}) {
  const lastNonce = useRef(0);
  if (nonce !== lastNonce.current && controls.current) {
    lastNonce.current = nonce;
    const c = controls.current;
    if (target === 'earth') {
      c.target.set(0, 0, 0);
      c.object.position.copy(DEFAULT_CAMERA);
    } else if (target === 'mission') {
      if (missionFrame) {
        c.target.copy(missionFrame.target);
        c.object.position.copy(missionFrame.camera);
      }
    } else if (selectedPos) {
      const v = new Vector3(selectedPos[0], selectedPos[1], selectedPos[2]);
      c.target.copy(v);
      // Stand off along the object's own radial direction so Earth stays behind it.
      c.object.position.copy(v.clone().multiplyScalar(1.55).add(new Vector3(0, 0.25, 0)));
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

  const missionFrame = useMemo<MissionFrame | null>(() => {
    if (!snapshot) return null;
    // A mission the operator selected wins; otherwise the first one still in flight.
    const mission =
      (selected?.kind === 'mission' ? snapshot.missions.find((m) => m.id === selected.id) : undefined) ??
      snapshot.missions.find((m) => LIVE_MISSION_STATES.has(m.state));
    const leg = mission?.legs.find((l) => l.status === 'current') ?? mission?.legs.at(-1);
    if (!leg) return null;
    return frameLeg(
      eciToSceneExaggerated(leg.fromPosition, altitudeExaggeration),
      eciToSceneExaggerated(leg.toPosition, altitudeExaggeration),
    );
  }, [snapshot, selected, altitudeExaggeration]);

  /** A tag on each live rescue's vehicle and on the rendezvous point at the end of its last leg. */
  const missionTags = useMemo<MissionTag[]>(() => {
    if (!snapshot) return [];
    const tags: MissionTag[] = [];
    for (const mission of snapshot.missions) {
      if (mission.completedAtSec !== null) continue;
      const vehicle = snapshot.rescueVehicles.find((v) => v.id === mission.vehicleId);
      const emergency = snapshot.emergencies.find((e) => e.id === mission.emergencyId);
      const target = snapshot.satellites.find((s) => s.id === emergency?.subjectId);
      const last = mission.legs.at(-1);
      if (vehicle) {
        tags.push({
          id: `${mission.id}-vehicle`,
          position: eciToSceneExaggerated(vehicle.position, altitudeExaggeration),
          text: `${vehicle.id} · ${mission.id}`,
          tone: 'vehicle',
        });
      }
      if (last) {
        tags.push({
          id: `${mission.id}-target`,
          position: eciToSceneExaggerated(last.toPosition, altitudeExaggeration),
          text: `⌖ ${target?.name ?? last.toEntityId} rendezvous`,
          tone: 'target',
        });
      }
    }
    return tags;
  }, [snapshot, altitudeExaggeration]);

  /** Orbit rings for the entities that matter right now — those on a live mission, and the
   * selection — rather than for every entity at once. Twenty-three full rings drawn together
   * formed a web across the globe that hid the transfers they were meant to explain. */
  const ringIds = useMemo(() => {
    const ids = new Set<string>();
    if (!snapshot) return ids;
    for (const mission of snapshot.missions) {
      if (!LIVE_MISSION_STATES.has(mission.state)) continue;
      ids.add(mission.vehicleId);
      const emergency = snapshot.emergencies.find((e) => e.id === mission.emergencyId);
      if (emergency) ids.add(emergency.subjectId);
    }
    if (selected?.kind === 'satellite' || selected?.kind === 'vehicle') ids.add(selected.id);
    return ids;
  }, [snapshot, selected]);

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
            snapshot.satellites
              .filter((sat) => ringIds.has(sat.id))
              .map((sat) => (
                <OrbitRing key={`orbit-sat-${sat.id}`} orbit={sat.orbit} exaggeration={altitudeExaggeration} />
              ))}
          {showOrbitRings &&
            snapshot.rescueVehicles
              .filter((v) => ringIds.has(v.id))
              .map((v) => (
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

          {/* Only rescues still in flight: finished missions' legs piled up as grey arcs around
              the planet over a long run and buried the one transfer that mattered. */}
          {showTrajectories &&
            snapshot.missions
              .filter((mission) => mission.completedAtSec === null)
              .flatMap((mission) =>
                mission.legs.map((leg, i) => (
                  <MissionLink key={`${mission.id}-leg-${i}`} leg={leg} exaggeration={altitudeExaggeration} />
                )),
              )}
          {showTrajectories && <MissionTags tags={missionTags} />}
        </>
      )}

      <CameraDirector
        controls={controlsRef}
        nonce={focusNonce}
        target={focusTarget}
        selectedPos={selectedPos ?? syntheticPos}
        missionFrame={missionFrame}
      />
      <OrbitControls ref={controlsRef} enablePan minDistance={1.2} maxDistance={30} />
    </Canvas>
  );
}

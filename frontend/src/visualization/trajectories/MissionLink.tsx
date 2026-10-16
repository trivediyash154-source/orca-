import { Line } from '@react-three/drei';

import { eciToSceneExaggerated } from '../coordinates';
import { legArc } from './legArc';
import type { MissionLegView } from '../../types/engine';

const STATUS_COLOR: Record<MissionLegView['status'], string> = {
  completed: '#6b7280',
  current: '#ffb84d',
  upcoming: '#4fa8ff',
};

export interface MissionLinkProps {
  readonly leg: MissionLegView;
  readonly exaggeration?: number;
}

/** One mission leg, drawn between its two REAL endpoints -- brief §44/§77: proving mission
 * and route data reaches the browser. Exact mid-leg position is not modelled anywhere in the
 * engine (`DYNAMIC_REPLANNING.md` §13.2), so the curve between the endpoints is a connector,
 * not a trajectory. It sweeps around the planet rather than cutting a chord through it: see
 * `legArc.ts` for why a straight line was the more misleading of the two. */
export function MissionLink({ leg, exaggeration = 1 }: MissionLinkProps) {
  const from = eciToSceneExaggerated(leg.fromPosition, exaggeration);
  const to = eciToSceneExaggerated(leg.toPosition, exaggeration);
  return (
    <Line
      points={legArc(from, to)}
      color={STATUS_COLOR[leg.status]}
      lineWidth={leg.status === 'current' ? 4 : 2}
      dashed={leg.status === 'upcoming'}
      dashSize={0.03}
      gapSize={0.02}
      transparent
      opacity={leg.status === 'completed' ? 0.3 : leg.status === 'current' ? 1 : 0.75}
    />
  );
}

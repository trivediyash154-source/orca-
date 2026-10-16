import { useMemo, useState } from 'react';

import { useEngine } from '../../state/EngineContext';
import { formatPercent } from '../../utils/format';
import { layoutNetwork, NETWORK_VIEWBOX_SIZE } from './networkLayout';
import type { GraphEdgeView } from '../../types/engine';

/** Brief §36-38: the actual mission graph behind the orbital scene, presentation-layout
 * only (`networkLayout.ts`), never re-running a graph algorithm in the browser. Active
 * mission routes are highlighted using each mission's real `legs`, matched by node
 * `entityId`, not recomputed. */
export function OrbitalNetworkPage() {
  const { snapshot, selected, select } = useEngine();
  const [hoveredEdge, setHoveredEdge] = useState<GraphEdgeView | null>(null);

  const points = useMemo(() => layoutNetwork(snapshot?.graph.nodes ?? []), [snapshot?.graph.nodes]);

  if (!snapshot) {
    return <div className="page-placeholder">No snapshot yet.</div>;
  }

  const nodeByEntityId = new Map(snapshot.graph.nodes.map((n) => [n.entityId, n]));
  const activeEdgeIds = new Set<string>();
  for (const mission of snapshot.missions) {
    for (const leg of mission.legs) {
      const from = nodeByEntityId.get(leg.fromEntityId);
      const to = nodeByEntityId.get(leg.toEntityId);
      if (!from || !to) continue;
      const edge = snapshot.graph.edges.find((e) => e.from === from.id && e.to === to.id);
      if (edge) activeEdgeIds.add(edge.id);
    }
  }

  const selectedNodeId =
    selected && (selected.kind === 'satellite' || selected.kind === 'vehicle' || selected.kind === 'graphNode')
      ? (snapshot.graph.nodes.find((n) => n.entityId === selected.id || n.id === selected.id)?.id ?? null)
      : null;

  return (
    <div className="network-page">
      <svg
        className="network-svg"
        viewBox={`0 0 ${NETWORK_VIEWBOX_SIZE} ${NETWORK_VIEWBOX_SIZE}`}
        role="img"
        aria-label="Orbital network graph"
      >
        {snapshot.graph.edges.map((edge) => {
          const a = points.get(edge.from);
          const b = points.get(edge.to);
          if (!a || !b) return null;
          const active = activeEdgeIds.has(edge.id);
          return (
            <line
              key={edge.id}
              x1={a.x}
              y1={a.y}
              x2={b.x}
              y2={b.y}
              className={`network-edge${active ? ' network-edge-active' : ''}${!edge.feasible ? ' network-edge-infeasible' : ''}`}
              onMouseEnter={() => setHoveredEdge(edge)}
              onMouseLeave={() => setHoveredEdge((cur) => (cur?.id === edge.id ? null : cur))}
            />
          );
        })}
        {snapshot.graph.nodes.map((node) => {
          const p = points.get(node.id);
          if (!p) return null;
          const isSelected = node.id === selectedNodeId;
          return (
            <g key={node.id} transform={`translate(${p.x},${p.y})`}>
              <circle
                r={isSelected ? 9 : 6}
                className={`network-node network-node-${node.kind.toLowerCase()}`}
                onClick={() =>
                  select({
                    kind: node.kind === 'SATELLITE' ? 'satellite' : 'vehicle',
                    id: node.entityId,
                  })
                }
              />
              <text className="network-node-label" x={0} y={-12}>
                {node.entityId}
              </text>
            </g>
          );
        })}
      </svg>

      <aside className="network-detail">
        <h2>Transfer</h2>
        {hoveredEdge ? (
          <dl>
            <dt>Time</dt>
            <dd>{(hoveredEdge.cost.time / 60).toFixed(1)} min</dd>
            <dt>Fuel</dt>
            <dd>{formatPercent(hoveredEdge.cost.fuel)}</dd>
            <dt>Risk</dt>
            <dd>{formatPercent(hoveredEdge.cost.risk)}</dd>
            <dt>Distance</dt>
            <dd>{hoveredEdge.cost.distance.toFixed(0)} km</dd>
            <dt>Feasible</dt>
            <dd>{hoveredEdge.feasible ? 'Yes' : 'No'}</dd>
          </dl>
        ) : (
          <p className="muted">Hover an edge to inspect its transfer cost.</p>
        )}
        <p className="muted network-footnote">
          Layout is presentation-only; it does not represent orbital geometry (see Mission
          Control for that). Edge costs are build-time reference samples, illustrative only.
        </p>
      </aside>
    </div>
  );
}

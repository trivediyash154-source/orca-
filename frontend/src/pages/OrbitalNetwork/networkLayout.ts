/**
 * A deterministic, presentation-only 2D layout for the mission graph — brief §37: "the
 * frontend may position nodes based on a visualization layout... do not claim it
 * represents the mission graph's algorithmic structure." No force simulation, no physics:
 * vehicles on an inner ring, satellites on an outer ring, evenly spaced by index within
 * each ring. This is a rendering convenience, not a claim about orbital geometry (that is
 * what Mission Control's own 3D scene already shows honestly).
 */

import type { GraphNodeView } from '../../types/engine';

export interface LayoutPoint {
  readonly id: string;
  readonly x: number;
  readonly y: number;
}

const INNER_RADIUS = 120;
const OUTER_RADIUS = 220;
const CENTER = 260;

export function layoutNetwork(nodes: readonly GraphNodeView[]): Map<string, LayoutPoint> {
  const vehicles = nodes.filter((n) => n.kind === 'RESCUE_VEHICLE');
  const satellites = nodes.filter((n) => n.kind === 'SATELLITE');
  const points = new Map<string, LayoutPoint>();

  const place = (group: readonly GraphNodeView[], radius: number) => {
    group.forEach((node, i) => {
      const angle = (i / Math.max(group.length, 1)) * 2 * Math.PI - Math.PI / 2;
      points.set(node.id, {
        id: node.id,
        x: CENTER + radius * Math.cos(angle),
        y: CENTER + radius * Math.sin(angle),
      });
    });
  };

  place(vehicles, INNER_RADIUS);
  place(satellites, OUTER_RADIUS);
  return points;
}

export const NETWORK_VIEWBOX_SIZE = CENTER * 2;

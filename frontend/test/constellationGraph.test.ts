/**
 * The link graph and the searches over it. These run live in the Optimization Lab, so each
 * property a reader is told about on screen — no link through the Earth, A* matches Dijkstra's
 * route, A* expands no more than Dijkstra — is asserted here on real constellation geometry.
 */

import { describe, expect, it } from 'vitest';

import {
  EARTH_RADIUS_KM,
  aStar,
  buildLinks,
  components,
  dijkstra,
  lineOfSight,
} from '../src/pages/OptimizationLab/constellationGraph';
import { demoVisualConstellation } from '../src/services/demoConstellation';

const leo = demoVisualConstellation(1_200, 7, 0).satellites.filter((s) => s.band === 'LEO');
const positions = leo.map((s) => s.position);

describe('constellationGraph', () => {
  it('rejects a link through the planet and accepts one above it', () => {
    const r = EARTH_RADIUS_KM + 550;
    expect(lineOfSight({ x: r, y: 0, z: 0 }, { x: -r, y: 0, z: 0 })).toBe(false);
    expect(lineOfSight({ x: r, y: 0, z: 0 }, { x: r * Math.cos(0.1), y: r * Math.sin(0.1), z: 0 })).toBe(true);
  });

  it('builds links that are all within range and all clear of the Earth', () => {
    const graph = buildLinks(positions, 1_500, 4);
    expect(graph.links.length).toBeGreaterThan(positions.length);
    for (const link of graph.links) {
      expect(link.lengthKm).toBeLessThanOrEqual(1_500);
      expect(lineOfSight(positions[link.a]!, positions[link.b]!)).toBe(true);
    }
  });

  it('never duplicates a link or links a node to itself', () => {
    const graph = buildLinks(positions, 1_500, 4);
    const ids = new Set(graph.links.map((l) => `${l.a}-${l.b}`));
    expect(ids.size).toBe(graph.links.length);
    expect(graph.links.every((l) => l.a < l.b)).toBe(true);
  });

  it('counts components consistently with the links', () => {
    const graph = buildLinks(positions, 1_500, 4);
    const summary = components(positions.length, graph.links);
    for (const { a, b } of graph.links) expect(summary.rootOf[a]).toBe(summary.rootOf[b]);
    expect(summary.largest).toBeLessThanOrEqual(positions.length);
    expect(summary.count).toBeGreaterThanOrEqual(1);
  });

  it('finds the same optimal route length with A* as with Dijkstra, expanding no more nodes', () => {
    const graph = buildLinks(positions, 2_200, 6);
    const summary = components(positions.length, graph.links);
    // Pick a target in the source's component so a route exists.
    const source = 0;
    const target = positions.findIndex(
      (_, i) => i > positions.length / 2 && summary.rootOf[i] === summary.rootOf[source],
    );
    expect(target).toBeGreaterThan(0);

    const d = dijkstra(positions, graph, source, target);
    const a = aStar(positions, graph, source, target);
    expect(d.found && a.found).toBe(true);
    expect(a.lengthKm).toBeCloseTo(d.lengthKm, 6);
    expect(a.nodesExpanded).toBeLessThanOrEqual(d.nodesExpanded);
    expect(a.path[0]).toBe(source);
    expect(a.path[a.path.length - 1]).toBe(target);
  });

  it('reports no route across disconnected components rather than inventing one', () => {
    const tiny = [
      { x: EARTH_RADIUS_KM + 500, y: 0, z: 0 },
      { x: -(EARTH_RADIUS_KM + 500), y: 0, z: 0 },
    ];
    const graph = buildLinks(tiny, 1_000, 2);
    expect(dijkstra(tiny, graph, 0, 1).found).toBe(false);
    expect(aStar(tiny, graph, 0, 1).path).toEqual([]);
  });
});

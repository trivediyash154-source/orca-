/**
 * The inter-satellite link graph behind the Optimization Lab's constellation network, and the
 * searches run over it — all computed in the browser, on every frame that needs it.
 *
 * Unlike the panels fed by the snapshot, nothing here is replayed: every link, component and
 * route is computed live from the positions on screen. That makes this module the one place in
 * the frontend that runs real graph algorithms, so it is written the way the engine's are —
 * explicit data structures, stated complexities, and counters that report what the search did.
 *
 *   buildLinks        uniform 3-D grid spatial hash, then k-nearest within range   O(N) expected
 *   lineOfSight       segment–sphere test so no link passes through the Earth      O(1)
 *   components        union-find with path halving and union by size              O(E α(N))
 *   dijkstra / aStar  binary min-heap, lazy deletion, shared relaxation counters   O((V+E) log V)
 *
 * A* uses straight-line distance to the target as its heuristic. Edge weights are straight-line
 * link lengths, so the triangle inequality makes that heuristic admissible and consistent: A*
 * returns the same optimal route Dijkstra does, and the difference between them is purely in
 * how much of the graph each had to touch — which is exactly what the lab displays.
 */

export interface Vec3 {
  readonly x: number;
  readonly y: number;
  readonly z: number;
}

export interface Link {
  readonly a: number;
  readonly b: number;
  readonly lengthKm: number;
}

export interface LinkGraph {
  readonly links: readonly Link[];
  /** Adjacency list: for node `i`, the indices into `links` touching it. */
  readonly adjacency: readonly (readonly number[])[];
  readonly gridCells: number;
  readonly candidatePairsTested: number;
  readonly occludedPairs: number;
  readonly buildMs: number;
}

export const EARTH_RADIUS_KM = 6_371;
/** A link that grazes the atmosphere is as unusable as one through the crust: optical and RF
 * crosslinks both degrade badly below roughly 80-100 km of tangent altitude. */
const LINK_CLEARANCE_KM = 100;

function now(): number {
  return typeof performance === 'undefined' ? Date.now() : performance.now();
}

function dist(a: Vec3, b: Vec3): number {
  const dx = a.x - b.x;
  const dy = a.y - b.y;
  const dz = a.z - b.z;
  return Math.sqrt(dx * dx + dy * dy + dz * dz);
}

/**
 * True when the straight segment a→b stays above the Earth plus clearance. Closest point of
 * the segment to the origin, clamped to the segment — the standard segment–sphere test.
 */
export function lineOfSight(a: Vec3, b: Vec3, clearanceKm = LINK_CLEARANCE_KM): boolean {
  const dx = b.x - a.x;
  const dy = b.y - a.y;
  const dz = b.z - a.z;
  const lengthSq = dx * dx + dy * dy + dz * dz;
  if (lengthSq === 0) return true;
  const t = Math.max(0, Math.min(1, -(a.x * dx + a.y * dy + a.z * dz) / lengthSq));
  const px = a.x + t * dx;
  const py = a.y + t * dy;
  const pz = a.z + t * dz;
  const limit = EARTH_RADIUS_KM + clearanceKm;
  return px * px + py * py + pz * pz > limit * limit;
}

/**
 * Builds the link graph with a uniform 3-D grid whose cell edge equals the link range, so every
 * candidate partner of a node lies in its own cell or one of the 26 around it.
 *
 * A grid rather than a k-d tree for the reason `docs/ALGORITHMS.md` §9 gives: these positions
 * change every frame. A tree pays O(N log N) to rebuild for O(log N) queries; the grid rebuilds
 * in O(N) and answers each neighbourhood query in expected O(1) at bounded density.
 */
export function buildLinks(
  positions: readonly Vec3[],
  rangeKm: number,
  maxLinksPerNode: number,
): LinkGraph {
  const started = now();
  const cell = rangeKm;
  const grid = new Map<string, number[]>();

  const keyOf = (ix: number, iy: number, iz: number) => `${ix},${iy},${iz}`;
  const cellOf = (p: Vec3): [number, number, number] => [
    Math.floor(p.x / cell),
    Math.floor(p.y / cell),
    Math.floor(p.z / cell),
  ];

  positions.forEach((p, i) => {
    const [ix, iy, iz] = cellOf(p);
    const key = keyOf(ix, iy, iz);
    const bucket = grid.get(key);
    if (bucket) bucket.push(i);
    else grid.set(key, [i]);
  });

  let tested = 0;
  let occluded = 0;
  // Each node proposes its k nearest visible partners; a link exists if either end proposed it.
  // That union keeps the graph undirected without letting one dense cluster monopolise degree.
  const seen = new Set<number>();
  const links: Link[] = [];
  const n = positions.length;

  for (let i = 0; i < n; i += 1) {
    const p = positions[i]!;
    const [ix, iy, iz] = cellOf(p);
    const candidates: { j: number; d: number }[] = [];

    for (let ox = -1; ox <= 1; ox += 1) {
      for (let oy = -1; oy <= 1; oy += 1) {
        for (let oz = -1; oz <= 1; oz += 1) {
          const bucket = grid.get(keyOf(ix + ox, iy + oy, iz + oz));
          if (!bucket) continue;
          for (const j of bucket) {
            if (j === i) continue;
            tested += 1;
            const d = dist(p, positions[j]!);
            if (d <= rangeKm) candidates.push({ j, d });
          }
        }
      }
    }

    candidates.sort((u, v) => u.d - v.d || u.j - v.j);
    let accepted = 0;
    for (const { j, d } of candidates) {
      if (accepted >= maxLinksPerNode) break;
      if (!lineOfSight(p, positions[j]!)) {
        occluded += 1;
        continue;
      }
      accepted += 1;
      const lo = Math.min(i, j);
      const hi = Math.max(i, j);
      const id = lo * n + hi;
      if (seen.has(id)) continue;
      seen.add(id);
      links.push({ a: lo, b: hi, lengthKm: d });
    }
  }

  const adjacency: number[][] = Array.from({ length: n }, () => []);
  links.forEach((link, index) => {
    adjacency[link.a]!.push(index);
    adjacency[link.b]!.push(index);
  });

  return {
    links,
    adjacency,
    gridCells: grid.size,
    candidatePairsTested: tested,
    occludedPairs: occluded,
    buildMs: now() - started,
  };
}

/* ======================================================================= union-find ===== */

export interface ComponentSummary {
  readonly count: number;
  readonly largest: number;
  /** Component id per node — the root index. */
  readonly rootOf: Int32Array;
}

/** Disjoint-set forest with path halving and union by size: near-constant amortised cost per
 * operation (inverse Ackermann), which is why it is the standard tool for connectivity. */
export function components(nodeCount: number, links: readonly Link[]): ComponentSummary {
  const parent = new Int32Array(nodeCount);
  const size = new Int32Array(nodeCount).fill(1);
  for (let i = 0; i < nodeCount; i += 1) parent[i] = i;

  const find = (x: number): number => {
    let node = x;
    while (parent[node] !== node) {
      parent[node] = parent[parent[node]!]!;
      node = parent[node]!;
    }
    return node;
  };

  for (const { a, b } of links) {
    let ra = find(a);
    let rb = find(b);
    if (ra === rb) continue;
    if (size[ra]! < size[rb]!) [ra, rb] = [rb, ra];
    parent[rb] = ra;
    size[ra] = size[ra]! + size[rb]!;
  }

  const rootOf = new Int32Array(nodeCount);
  let count = 0;
  let largest = 0;
  for (let i = 0; i < nodeCount; i += 1) {
    const root = find(i);
    rootOf[i] = root;
    if (root === i) {
      count += 1;
      largest = Math.max(largest, size[i]!);
    }
  }
  return { count, largest, rootOf };
}

/* ======================================================================= binary heap ==== */

/** Array-backed binary min-heap keyed on `priority`, ties broken by node index so two runs over
 * the same graph pop in the same order — the determinism rule the engine's searches follow. */
class MinHeap {
  private readonly keys: number[] = [];
  private readonly nodes: number[] = [];

  get size(): number {
    return this.keys.length;
  }

  push(priority: number, node: number): void {
    this.keys.push(priority);
    this.nodes.push(node);
    let i = this.keys.length - 1;
    while (i > 0) {
      const parent = (i - 1) >> 1;
      if (!this.less(i, parent)) break;
      this.swap(i, parent);
      i = parent;
    }
  }

  pop(): { priority: number; node: number } | null {
    if (this.keys.length === 0) return null;
    const top = { priority: this.keys[0]!, node: this.nodes[0]! };
    const lastKey = this.keys.pop()!;
    const lastNode = this.nodes.pop()!;
    if (this.keys.length > 0) {
      this.keys[0] = lastKey;
      this.nodes[0] = lastNode;
      let i = 0;
      for (;;) {
        const left = 2 * i + 1;
        const right = left + 1;
        let best = i;
        if (left < this.keys.length && this.less(left, best)) best = left;
        if (right < this.keys.length && this.less(right, best)) best = right;
        if (best === i) break;
        this.swap(i, best);
        i = best;
      }
    }
    return top;
  }

  private less(i: number, j: number): boolean {
    const a = this.keys[i]!;
    const b = this.keys[j]!;
    return a < b || (a === b && this.nodes[i]! < this.nodes[j]!);
  }

  private swap(i: number, j: number): void {
    [this.keys[i], this.keys[j]] = [this.keys[j]!, this.keys[i]!];
    [this.nodes[i], this.nodes[j]] = [this.nodes[j]!, this.nodes[i]!];
  }
}

/* ======================================================================= searches ======= */

export interface SearchResult {
  readonly algorithm: 'DIJKSTRA' | 'ASTAR';
  readonly found: boolean;
  /** Node indices from source to target inclusive; empty when not found. */
  readonly path: readonly number[];
  readonly lengthKm: number;
  readonly nodesExpanded: number;
  readonly edgesRelaxed: number;
  readonly heapPushes: number;
  readonly staleDiscarded: number;
  readonly ms: number;
  /** Every node the search settled, for drawing the explored region. */
  readonly settled: Uint8Array;
}

function search(
  algorithm: 'DIJKSTRA' | 'ASTAR',
  positions: readonly Vec3[],
  graph: LinkGraph,
  source: number,
  target: number,
): SearchResult {
  const started = now();
  const n = positions.length;
  const distance = new Float64Array(n).fill(Number.POSITIVE_INFINITY);
  const previous = new Int32Array(n).fill(-1);
  const settled = new Uint8Array(n);
  const heap = new MinHeap();
  const goal = positions[target]!;
  const h = (i: number) => (algorithm === 'ASTAR' ? dist(positions[i]!, goal) : 0);

  let expanded = 0;
  let relaxed = 0;
  let pushes = 1;
  let stale = 0;

  distance[source] = 0;
  heap.push(h(source), source);

  while (heap.size > 0) {
    const entry = heap.pop()!;
    const u = entry.node;
    // Lazy deletion: a node may sit in the heap several times with stale priorities. Skipping
    // the stale copies on pop is cheaper than a decrease-key a binary heap cannot do in place.
    if (settled[u]) {
      stale += 1;
      continue;
    }
    settled[u] = 1;
    expanded += 1;
    if (u === target) break;

    for (const linkIndex of graph.adjacency[u]!) {
      const link = graph.links[linkIndex]!;
      const v = link.a === u ? link.b : link.a;
      if (settled[v]) continue;
      relaxed += 1;
      const candidate = distance[u]! + link.lengthKm;
      if (candidate < distance[v]!) {
        distance[v] = candidate;
        previous[v] = u;
        heap.push(candidate + h(v), v);
        pushes += 1;
      }
    }
  }

  const found = Number.isFinite(distance[target]!);
  const path: number[] = [];
  if (found) {
    for (let node = target; node !== -1; node = previous[node]!) path.push(node);
    path.reverse();
  }

  return {
    algorithm,
    found,
    path,
    lengthKm: found ? distance[target]! : 0,
    nodesExpanded: expanded,
    edgesRelaxed: relaxed,
    heapPushes: pushes,
    staleDiscarded: stale,
    ms: now() - started,
    settled,
  };
}

export function dijkstra(positions: readonly Vec3[], graph: LinkGraph, s: number, t: number) {
  return search('DIJKSTRA', positions, graph, s, t);
}

export function aStar(positions: readonly Vec3[], graph: LinkGraph, s: number, t: number) {
  return search('ASTAR', positions, graph, s, t);
}

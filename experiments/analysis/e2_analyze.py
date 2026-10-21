import csv, collections, numpy as np, moocore
from scipy import stats

Q = list(csv.DictReader(open('results/e2/queries.csv')))
F = collections.defaultdict(lambda: collections.defaultdict(list))
for r in csv.DictReader(open('results/e2/fronts.csv')):
    F[int(r['qid'])][r['method']].append([float(r['time']), float(r['fuel']), float(r['risk']), float(r['distance'])])
sizes = sorted({int(q['size']) for q in Q})
f = lambda q, k: float(q[k])

print("=== median wall-clock (ms) and effort, pooled over families ===")
print(" size   n  nodes  edges(med)  build   Dijkstra   A*     Martins   NAMOA*   eps0.01  eps0.05  eps0.10 | A* exp saving  NAMOA*/Martins time  labels gen ratio  equal")
for s in sizes:
    qs = [q for q in Q if int(q['size']) == s]
    med = lambda k: np.median([f(q, k) for q in qs])
    found = [q for q in qs if q['dij_found'] == '1' and int(q['dij_expanded']) > 0]
    sav = np.median([1 - f(q, 'astar_expanded') / f(q, 'dij_expanded') for q in found])
    pf = [q for q in qs if f(q, 'martins_ms') > 0]
    tr = np.median([f(q, 'namoa_ms') / f(q, 'martins_ms') for q in pf])
    lr = np.median([f(q, 'namoa_generated') / max(f(q, 'martins_generated'), 1) for q in pf])
    eq = sum(q['namoa_equal'] == '1' for q in qs)
    print(f"{s:5d} {len(qs):3d} {med('nodes'):6.0f} {med('edges'):10.0f} {med('build_ms'):7.1f} {med('dij_ms'):9.3f} {med('astar_ms'):8.3f} {med('martins_ms'):9.2f} {med('namoa_ms'):8.2f} {med('eps01_ms'):8.2f} {med('eps05_ms'):8.2f} {med('eps10_ms'):8.2f} | {100*sav:6.1f}%  {tr:8.2f}  {lr:8.2f}  {eq}/{len(qs)}")

print("\n=== log-log scaling fits: time = a * N^b (instance level), with 95% CI on b ===")
for k, lab in [('dij_ms', 'Dijkstra'), ('astar_ms', 'A*'), ('martins_ms', 'Martins'), ('namoa_ms', 'NAMOA*'), ('build_ms', 'graph build')]:
    xs = np.array([np.log(f(q, 'size')) for q in Q if f(q, k) > 0]); ys = np.array([np.log(f(q, k)) for q in Q if f(q, k) > 0])
    r = stats.linregress(xs, ys); ci = 1.96 * r.stderr
    xe = np.array([np.log(f(q, 'edges')) for q in Q if f(q, k) > 0]); re_ = stats.linregress(xe, ys)
    print(f"  {lab:12s} b = {r.slope:.2f} [{r.slope-ci:.2f}, {r.slope+ci:.2f}]  R^2={r.rvalue**2:.2f}   vs edges: {re_.slope:.2f} +- {1.96*re_.stderr:.2f}")

print("\n=== by family at the largest size ===")
smax = max(sizes)
for fam in sorted({q['family'] for q in Q}):
    qs = [q for q in Q if int(q['size']) == smax and q['family'] == fam]
    if not qs: continue
    print(f"  {fam:16s} Martins {np.median([f(q,'martins_ms') for q in qs]):9.1f} ms  NAMOA* {np.median([f(q,'namoa_ms') for q in qs]):9.1f} ms  front {np.median([f(q,'martins_front') for q in qs]):.0f}  truncated {sum(q['martins_truncated']=='1' for q in qs)}")

print("\n=== epsilon-dominance: front size, time and hypervolume relative to the exact front ===")
for s in sizes:
    rows = collections.defaultdict(list)
    for q in Q:
        if int(q['size']) != s: continue
        ex = np.array(F[int(q['qid'])]['exact'])
        if len(ex) < 2: continue
        lo, hi = ex.min(0), ex.max(0); rng = np.where(hi - lo > 1e-12, hi - lo, 1.0)
        ref = np.ones(4) * 1.1
        hv_ex = moocore.hypervolume((ex - lo) / rng, ref=ref)
        for m, tk, fk in [('eps0.01', 'eps01_ms', 'eps01_front'), ('eps0.05', 'eps05_ms', 'eps05_front'), ('eps0.10', 'eps10_ms', 'eps10_front')]:
            ep = np.array(F[int(q['qid'])][m])
            hv = moocore.hypervolume(np.minimum((ep - lo) / rng, 1.1), ref=ref) if len(ep) else 0.0
            rows[m].append((len(ep) / len(ex), f(q, tk) / f(q, 'martins_ms'), hv / hv_ex))
    for m, v in rows.items():
        v = np.array(v)
        print(f"  size {s:4d} {m}: n={len(v)} front kept {100*np.median(v[:,0]):5.1f}%  time {100*np.median(v[:,1]):5.1f}% of exact  hypervolume kept {100*np.median(v[:,2]):6.2f}% (min {100*v[:,2].min():.2f}%)")

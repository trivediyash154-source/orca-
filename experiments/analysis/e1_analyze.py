import csv, collections, math, json, sys
import numpy as np
from scipy.optimize import linprog
from scipy.stats import binomtest

Q = list(csv.DictReader(open('results/e1/queries.csv')))
F = collections.defaultdict(list)
for r in csv.DictReader(open('results/e1/fronts.csv')):
    F[int(r['qid'])].append(np.array([float(r['time']), float(r['fuel']), float(r['risk']), float(r['distance'])]))
EPS = 1e-6
def wilson(k, n, z=1.96):
    if n == 0: return (float('nan'),)*2
    p = k/n; d = 1+z*z/n; c = p+z*z/(2*n); h = z*math.sqrt(p*(1-p)/n+z*z/(4*n*n))
    return ((c-h)/d, (c+h)/d)

def classify(q, T=math.inf):
    """Outcome for one query under deadline T (seconds)."""
    front = [p for p in F[int(q['qid'])] if p[0] <= T + 1e-6]
    dfound = q['dij_found'] == '1' and float(q['dij_arrival']) <= T + 1e-6
    dover = dfound and float(q['dij_fuel']) > 1 + EPS
    rcfound = q['rc_found'] == '1' and float(q['rc_arrival']) <= T + 1e-6
    feasible = len(front) > 0
    exact_min = min(p[0] for p in front) if feasible else None
    if not dfound:
        d = 'none'
    elif not dover:
        d = 'ok'
    else:
        d = 'overbudget_infeasible' if not feasible else 'overbudget_feasible'
    if not feasible:
        rc = 'correct_none' if not rcfound else 'BUG'
    elif not rcfound:
        rc = 'false_none'
    elif abs(float(q['rc_arrival']) - exact_min) <= 1e-3:
        rc = 'optimal'
    else:
        rc = 'late'
    return d, rc, feasible, front

out = {}
for T, lab in [(math.inf, 'none'), (72*3600, '72h'), (48*3600, '48h'), (24*3600, '24h')]:
    rows = []
    for size in ['25', '50', '100', '200', 'all']:
        qs = [q for q in Q if size == 'all' or q['size'] == size]
        c = collections.Counter(); rcc = collections.Counter()
        for q in qs:
            d, rc, feas, _ = classify(q, T)
            c[d] += 1; rcc[rc] += 1
        n = len(qs)
        dfound = n - c['none']
        over = c['overbudget_infeasible'] + c['overbudget_feasible']
        rows.append(dict(size=size, n=n, dij_found=dfound, dij_over=over,
                         over_ci=wilson(over, dfound), over_infeasible=c['overbudget_infeasible'],
                         over_feasible=c['overbudget_feasible'], feasible=sum(1 for q in qs if classify(q, T)[2]),
                         rc=dict(rcc)))
    out[lab] = rows

print("=== Dijkstra/A* route vs cumulative budget, by deadline and size ===")
for lab, rows in out.items():
    print(f"-- deadline {lab}")
    for r in rows:
        lo, hi = r['over_ci']
        print(f"  size {r['size']:>4}: n={r['n']}, exact-feasible={r['feasible']}, dijkstra returns route={r['dij_found']}, "
              f"over budget={r['dij_over']} ({100*r['dij_over']/max(r['dij_found'],1):.1f}% [{100*lo:.1f},{100*hi:.1f}]) "
              f"-> no flyable route exists: {r['over_infeasible']}, flyable route exists: {r['over_feasible']}; RC-Dijkstra: {r['rc']}")

# per family (no deadline)
print("\n=== by family (no deadline) ===")
for fam in sorted(set(q['family'] for q in Q)):
    qs = [q for q in Q if q['family'] == fam]
    c = collections.Counter(classify(q)[0] for q in qs)
    dfound = len(qs) - c['none']; over = c['overbudget_infeasible'] + c['overbudget_feasible']
    print(f"  {fam:16s} n={len(qs)} dij_found={dfound} over={over} ({100*over/max(dfound,1):.1f}%) infeasible={c['overbudget_infeasible']} feasible_alt={c['overbudget_feasible']}")

# RC-Dijkstra lateness when late
late = []
for q in Q:
    d, rc, feas, front = classify(q)
    if rc == 'late':
        late.append((float(q['rc_arrival']) - min(p[0] for p in front)) / 3600)
print("\nRC-Dijkstra late cases:", len(late), "median extra h %.2f, max %.2f" % (np.median(late), max(late)) if late else "")

# Front sizes
print("\n=== front sizes (feasible queries, no deadline) ===")
for size in ['25', '50', '100', '200']:
    s = [len(F[int(q['qid'])]) for q in Q if q['size'] == size and len(F[int(q['qid'])]) > 0]
    print(f"  size {size}: n={len(s)} median={np.median(s):.0f} IQR=[{np.percentile(s,25):.0f},{np.percentile(s,75):.0f}] max={max(s)} share>1={np.mean(np.array(s)>1):.2f}")

# Unsupported points: p is supported iff some w in simplex makes p optimal for w.c over the front
def unsupported(front):
    P = np.array(front)
    lo, hi = P.min(0), P.max(0)
    keep = hi - lo > 1e-12
    if keep.sum() == 0: return 0, len(P)
    Z = (P[:, keep] - lo[keep]) / (hi[keep] - lo[keep])
    k = Z.shape[1]; uns = 0
    for i in range(len(Z)):
        A = Z[i] - Z  # rows: p - q ; need w.(p-q) <= 0
        res = linprog(np.zeros(k), A_ub=A, b_ub=np.full(len(Z), 1e-9), A_eq=np.ones((1, k)), b_eq=[1], bounds=[(0, None)]*k, method='highs')
        if res.status != 0: uns += 1
    return uns, len(Z)
print("\n=== unsupported plans (no weighted sum selects them, any weights) ===")
for size in ['25', '50', '100', '200', 'all']:
    tot = un = fronts_with = fronts = 0
    for q in Q:
        if size != 'all' and q['size'] != size: continue
        fr = F[int(q['qid'])]
        if len(fr) < 3: continue
        u, n = unsupported(fr); tot += n; un += u; fronts += 1; fronts_with += (u > 0)
    print(f"  size {size}: fronts(>=3 plans)={fronts}, plans={tot}, unsupported={un} ({100*un/max(tot,1):.1f}%), fronts containing >=1 unsupported={fronts_with} ({100*fronts_with/max(fronts,1):.1f}%)")

# Profile selections: do the four ORCA profiles pick different plans?
prof = {'FASTEST': (0.70, 0.10, 0.15, 0.05), 'FUEL_SAVING': (0.10, 0.70, 0.15, 0.05),
        'SAFETY_FIRST': (0.15, 0.10, 0.70, 0.05), 'BALANCED': (0.30, 0.30, 0.30, 0.10)}
dist = collections.Counter()
for q in Q:
    fr = F[int(q['qid'])]
    if len(fr) < 2: continue
    P = np.array(fr); lo, hi = P.min(0), P.max(0); rng = np.where(hi - lo > 1e-12, hi - lo, 1)
    Z = (P - lo) / rng
    picks = {int(np.argmin(Z @ np.array(w))) for w in prof.values()}
    dist[len(picks)] += 1
print("\nDistinct plans chosen by the 4 profiles (fronts with >=2 plans):", dict(sorted(dist.items())))

# fuel saved by the fuel-saving pick vs the fastest plan, and time cost of that
ratios = []
for q in Q:
    fr = F[int(q['qid'])]
    if len(fr) < 2: continue
    P = np.array(fr)
    fast = P[np.argmin(P[:, 0])]; cheap = P[np.argmin(P[:, 1])]
    if fast[1] > 0 and fast[0] > 0:
        ratios.append(((fast[1] - cheap[1]) / fast[1], (cheap[0] - fast[0]) / 3600))
r = np.array(ratios)
print("Fastest vs cheapest plan on the same front: median fuel saving %.1f%%, median extra time %.1f h (n=%d)" % (100*np.median(r[:,0]), np.median(r[:,1]), len(r)))
json.dump(out, open('results/e1/summary.json', 'w'), indent=1, default=str)

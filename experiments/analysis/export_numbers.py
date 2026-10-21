"""Every number quoted in the paper, computed from the raw result files."""
import csv, collections, json, math, numpy as np, moocore
from scipy import stats
from scipy.optimize import linprog
from scipy.sparse import csr_matrix
from scipy.sparse.csgraph import maximum_bipartite_matching
from scipy.stats import wilcoxon, binomtest
R = 'results/'
D = {}
EPS = 1e-6
def wilson(k, n, z=1.96):
    p = k / n; d = 1 + z * z / n; c = p + z * z / (2 * n); h = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n))
    return [(c - h) / d, (c + h) / d]

# ---------------- E0
r = list(csv.DictReader(open(R + 'e0/e0_verify.csv')))
b = [x for x in r if x['size'] == '25']
comp = [x for x in b if x['brute_status'] == 'complete']
D['e0'] = dict(queries=len(r), namoa_equal=sum(x['namoa_equal'] == '1' for x in r), astar_equal=sum(x['astar_equal'] == '1' for x in r),
               brute_attempted=len(b), brute_complete=len(comp), brute_equal=sum(x['brute_equal'] == '1' for x in comp),
               brute_nonempty=sum(int(x['brute_size']) > 0 for x in comp), brute_points=sum(int(x['brute_size']) for x in comp),
               brute_budget=sum(x['brute_status'] == 'budget' for x in b), seeds=len({x['seed'] for x in r}))

# ---------------- E1
Q = list(csv.DictReader(open(R + 'e1/queries.csv')))
F = collections.defaultdict(list)
for x in csv.DictReader(open(R + 'e1/fronts.csv')):
    F[int(x['qid'])].append([float(x['time']), float(x['fuel']), float(x['risk']), float(x['distance'])])
def cls(q, T=math.inf):
    front = [p for p in F[int(q['qid'])] if p[0] <= T + 1e-6]
    df = q['dij_found'] == '1' and float(q['dij_arrival']) <= T + 1e-6
    over = df and float(q['dij_fuel']) > 1 + EPS
    rcf = q['rc_found'] == '1' and float(q['rc_arrival']) <= T + 1e-6
    if not front: rc = 'none_ok' if not rcf else 'bug'
    elif not rcf: rc = 'false_none'
    elif abs(float(q['rc_arrival']) - min(p[0] for p in front)) <= 1e-3: rc = 'optimal'
    else: rc = 'late'
    return df, over, len(front) > 0, rc, front
e1 = dict(queries=len(Q), by_size={}, by_family={}, deadlines={})
for s in ['25', '50', '100', '200']:
    qs = [q for q in Q if q['size'] == s]; c = [cls(q) for q in qs]
    found = sum(x[0] for x in c); over = sum(x[1] for x in c)
    fs = [len(F[int(q['qid'])]) for q in qs if F[int(q['qid'])]]
    e1['by_size'][s] = dict(n=len(qs), found=found, over=over, pct=100 * over / found, ci=[100 * v for v in wilson(over, found)],
                            front_median=float(np.median(fs)), front_max=max(fs), feasible=sum(x[2] for x in c))
c = [cls(q) for q in Q]
found = sum(x[0] for x in c); over = sum(x[1] for x in c)
e1['all'] = dict(found=found, over=over, pct=100 * over / found, ci=[100 * v for v in wilson(over, found)],
                 over_infeasible=sum(x[1] and not x[2] for x in c), over_alt=sum(x[1] and x[2] for x in c),
                 feasible=sum(x[2] for x in c), rc=dict(collections.Counter(x[3] for x in c)))
late = [(float(q['rc_arrival']) - min(p[0] for p in x[4])) / 3600 for q, x in zip(Q, c) if x[3] == 'late']
e1['rc_late_median_h'] = float(np.median(late)); e1['rc_late_max_h'] = float(max(late))
for fam in sorted({q['family'] for q in Q}):
    cc = [cls(q) for q in Q if q['family'] == fam]; fd = sum(x[0] for x in cc); ov = sum(x[1] for x in cc)
    e1['by_family'][fam] = dict(found=fd, over=ov, pct=100 * ov / fd)
for lab, T in [('72h', 72 * 3600), ('48h', 48 * 3600), ('24h', 24 * 3600)]:
    cc = [cls(q, T) for q in Q]; fd = sum(x[0] for x in cc); ov = sum(x[1] for x in cc)
    e1['deadlines'][lab] = dict(found=fd, over=ov, pct=100 * ov / fd, rc=dict(collections.Counter(x[3] for x in cc)), feasible=sum(x[2] for x in cc))
def unsupported(front):
    P = np.array(front); lo, hi = P.min(0), P.max(0); keep = hi - lo > 1e-12
    Z = (P[:, keep] - lo[keep]) / (hi[keep] - lo[keep]); k = Z.shape[1]; u = 0
    for i in range(len(Z)):
        res = linprog(np.zeros(k), A_ub=Z[i] - Z, b_ub=np.full(len(Z), 1e-9), A_eq=np.ones((1, k)), b_eq=[1], bounds=[(0, None)] * k, method='highs')
        u += res.status != 0
    return u, len(Z)
tot = un = fr = frw = 0
for q in Q:
    P = F[int(q['qid'])]
    if len(P) < 3: continue
    u, n = unsupported(P); tot += n; un += u; fr += 1; frw += u > 0
e1['unsupported'] = dict(plans=tot, unsupported=un, pct=100 * un / tot, fronts=fr, fronts_with=frw, fronts_with_pct=100 * frw / fr)
prof = [(0.70, 0.10, 0.15, 0.05), (0.10, 0.70, 0.15, 0.05), (0.15, 0.10, 0.70, 0.05), (0.30, 0.30, 0.30, 0.10)]
dist = collections.Counter(); sav = []
for q in Q:
    P = F[int(q['qid'])]
    if len(P) < 2: continue
    A = np.array(P); lo, hi = A.min(0), A.max(0); rg = np.where(hi - lo > 1e-12, hi - lo, 1); Z = (A - lo) / rg
    dist[len({int(np.argmin(Z @ np.array(w))) for w in prof})] += 1
    fast = A[np.argmin(A[:, 0])]; cheap = A[np.argmin(A[:, 1])]
    if fast[1] > 0: sav.append(((fast[1] - cheap[1]) / fast[1], (cheap[0] - fast[0]) / 3600))
e1['profiles'] = dict(fronts=sum(dist.values()), distinct={str(k): v for k, v in sorted(dist.items())},
                      at_most_two_pct=100 * (dist[1] + dist[2]) / sum(dist.values()))
sav = np.array(sav); e1['fast_vs_cheap'] = dict(n=len(sav), dv_saving_pct=100 * float(np.median(sav[:, 0])), extra_h=float(np.median(sav[:, 1])))
D['e1'] = e1

# ---------------- E2
E2 = list(csv.DictReader(open(R + 'e2/queries.csv')))
FF = collections.defaultdict(lambda: collections.defaultdict(list))
for x in csv.DictReader(open(R + 'e2/fronts.csv')):
    FF[int(x['qid'])][x['method']].append([float(x['time']), float(x['fuel']), float(x['risk']), float(x['distance'])])
f = lambda q, k: float(q[k])
e2 = dict(queries=len(E2), namoa_equal=sum(q['namoa_equal'] == '1' for q in E2), truncated=sum(q['martins_truncated'] == '1' for q in E2), rows=[], fits={}, eps={})
for s in sorted({int(q['size']) for q in E2}):
    qs = [q for q in E2 if int(q['size']) == s]; med = lambda k: float(np.median([f(q, k) for q in qs]))
    fd = [q for q in qs if q['dij_found'] == '1' and int(q['dij_expanded']) > 0]
    pf = [q for q in qs if f(q, 'martins_ms') > 0]
    fronts = [f(q, 'martins_front') for q in qs if f(q, 'martins_front') > 0]
    e2['rows'].append(dict(size=s, n=len(qs), seeds=len({q['seed'] for q in qs}), nodes=med('nodes'), edges=med('edges'), build=med('build_ms'),
                           dij=med('dij_ms'), astar=med('astar_ms'), martins=med('martins_ms'), namoa=med('namoa_ms'),
                           eps01=med('eps01_ms'), astar_saving=100 * float(np.median([1 - f(q, 'astar_expanded') / f(q, 'dij_expanded') for q in fd])),
                           namoa_ratio=float(np.median([f(q, 'namoa_ms') / f(q, 'martins_ms') for q in pf])),
                           front_median=float(np.median(fronts)) if fronts else 0.0))
for k in ['dij_ms', 'astar_ms', 'martins_ms', 'namoa_ms', 'build_ms']:
    v = [q for q in E2 if f(q, k) > 0]
    xs = np.log([f(q, 'size') for q in v]); ys = np.log([f(q, k) for q in v]); r_ = stats.linregress(xs, ys)
    xe = np.log([f(q, 'edges') for q in v]); re_ = stats.linregress(xe, ys)
    e2['fits'][k] = dict(b=r_.slope, lo=r_.slope - 1.96 * r_.stderr, hi=r_.slope + 1.96 * r_.stderr, b_edges=re_.slope, b_edges_ci=1.96 * re_.stderr)
for m, tk in [('eps0.01', 'eps01_ms'), ('eps0.05', 'eps05_ms'), ('eps0.10', 'eps10_ms')]:
    keep = []; tm = []; hv = []
    for q in E2:
        ex = np.array(FF[int(q['qid'])]['exact'])
        if len(ex) < 2: continue
        lo, hi = ex.min(0), ex.max(0); rg = np.where(hi - lo > 1e-12, hi - lo, 1.0); ref = np.ones(4) * 1.1
        h0 = moocore.hypervolume((ex - lo) / rg, ref=ref); ep = np.array(FF[int(q['qid'])][m])
        h1 = moocore.hypervolume(np.minimum((ep - lo) / rg, 1.1), ref=ref) if len(ep) else 0.0
        keep.append(len(ep) / len(ex)); tm.append(f(q, tk) / f(q, 'martins_ms')); hv.append(h1 / h0)
    e2['eps'][m] = dict(n=len(hv), front_kept=100 * float(np.median(keep)), time=100 * float(np.median(tm)), hv=100 * float(np.median(hv)), hv_min=100 * float(min(hv)), hv_p10=100 * float(np.percentile(hv, 10)))
fam500 = {}
for fam in sorted({q['family'] for q in E2}):
    qs = [q for q in E2 if int(q['size']) == 500 and q['family'] == fam]
    fam500[fam] = dict(martins=float(np.median([f(q, 'martins_ms') for q in qs])), namoa=float(np.median([f(q, 'namoa_ms') for q in qs])))
e2['fam500'] = fam500
D['e2'] = e2

# ---------------- E3
I = list(csv.DictReader(open(R + 'e3/instances.csv')))
cells = collections.defaultdict(list)
for x in csv.DictReader(open(R + 'e3/cells.csv')): cells[int(x['iid'])].append(x)
def mm(iid, n):
    rr = [int(c['row']) for c in cells[iid] if c['feasible'] == '1']; cc = [int(c['col']) for c in cells[iid] if c['feasible'] == '1']
    if not rr: return 0
    return int((maximum_bipartite_matching(csr_matrix((np.ones(len(rr)), (rr, cc)), shape=(n, n)), perm_type='column') >= 0).sum())
e3 = dict(instances=len(I), rows=[])
G = np.array([int(x['greedy_assigned']) for x in I]); H = np.array([int(x['hung_assigned']) for x in I]); M = np.array([mm(int(x['iid']), int(x['size'])) for x in I])
e3['hung_at_max'] = int((H == M).sum()); e3['greedy_below_max'] = int((G < M).sum()); e3['h_more'] = int((H > G).sum()); e3['h_fewer'] = int((H < G).sum())
e3['sign_p'] = binomtest(int((H > G).sum()), int((H > G).sum() + (H < G).sum())).pvalue
eq = [(float(x['greedy_cost']), float(x['hung_cost'])) for x in I if x['greedy_assigned'] == x['hung_assigned']]
rg = np.array([a - b_ for a, b_ in eq])
e3['eq_n'] = len(eq); e3['eq_cheaper'] = int((rg > 1e-9).sum()); e3['eq_same'] = int((np.abs(rg) <= 1e-9).sum()); e3['eq_dearer'] = int((rg < -1e-9).sum())
e3['eq_wilcoxon_p'] = wilcoxon([a for a, b_ in eq], [b_ for a, b_ in eq], alternative='greater').pvalue
for s in ['2', '5', '10', '20', '30']:
    idx = [i for i, x in enumerate(I) if x['size'] == s]
    g, h, m_ = G[idx], H[idx], M[idx]
    eqs = [(float(I[i]['greedy_cost']), float(I[i]['hung_cost'])) for i in idx if I[i]['greedy_assigned'] == I[i]['hung_assigned']]
    r2 = np.array([a - b_ for a, b_ in eqs]) if eqs else np.array([])
    e3['rows'].append(dict(size=int(s), n=len(idx), greedy_below=int((g < m_).sum()), mean_gap=float(np.mean(h - g)), max_gap=int(np.max(h - g)),
                           eq_n=len(eqs), eq_cheaper=int((r2 > 1e-9).sum()), med_regret=float(np.median(r2)) if len(r2) else 0.0,
                           hung_ms=float(np.median([float(I[i]['hung_ms']) for i in idx])), greedy_ms=float(np.median([float(I[i]['greedy_ms']) for i in idx]))))
D['e3'] = e3

# ---------------- E4 dispatch
rows = list(csv.DictReader(open(R + 'e4/dispatch_50.csv'))) + list(csv.DictReader(open(R + 'e4/dispatch_100.csv')))
by = collections.defaultdict(dict)
for x in rows: by[(x['satellites'], x['window_h'], x['cycle_s'], x['seed'])][x['solver']] = x
e4d = {}
for lab, cond in [('immediate', lambda k: k[2] == '0'), ('batched', lambda k: k[2] != '0'), ('batch3h', lambda k: k[2] == '10800'), ('batch1h', lambda k: k[2] == '3600')]:
    ps = [v for k, v in by.items() if cond(k)]
    Hh = lambda k: np.array([float(p['hungarian'][k]) for p in ps]); Gg = lambda k: np.array([float(p['greedy'][k]) for p in ps])
    sh, sg = Hh('served_on_time'), Gg('served_on_time'); gt, lt = int((sh > sg).sum()), int((sh < sg).sum())
    vh, vg = Hh('severity_served'), Gg('severity_served'); d = vh - vg; nz = d[np.abs(d) > 1e-9]
    e4d[lab] = dict(pairs=len(ps), more=gt, fewer=lt, served_h=float(sh.sum()), served_g=float(sg.sum()),
                    sign_p=binomtest(gt, gt + lt).pvalue if gt + lt else None, sev_h=float(vh.sum()), sev_g=float(vg.sum()),
                    sev_gain_pct=100 * (vh.sum() / vg.sum() - 1), sev_more=int((d > 0).sum()), sev_fewer=int((d < 0).sum()),
                    sev_p=wilcoxon(nz).pvalue if len(nz) else None, dv_med_diff=float(np.median(Hh('delta_v_kms') - Gg('delta_v_kms'))),
                    resp_h=float(Hh('mean_response_h').mean()), resp_g=float(Gg('mean_response_h').mean()))
imm = [v['hungarian'] for k, v in by.items() if k[2] == '0']; b3 = [v['hungarian'] for k, v in by.items() if k[2] == '10800']
e4d['hung_immediate_vs_3h'] = dict(sev_imm=sum(float(x['severity_served']) for x in imm), sev_b3=sum(float(x['severity_served']) for x in b3),
                                   resp_imm=float(np.mean([float(x['mean_response_h']) for x in imm])), resp_b3=float(np.mean([float(x['mean_response_h']) for x in b3])))
D['e4_dispatch'] = e4d

# ---------------- E4 hysteresis
e4h = {}
for sats in ['50', '100']:
    try: hr = list(csv.DictReader(open(R + f'e4/hyst_{sats}.csv')))
    except FileNotFoundError: continue
    byh = collections.defaultdict(list)
    for x in hr: byh[x['margin']].append(x)
    seeds_complete = min(len(v) for v in byh.values())
    tab = []
    for m in ['0', '0.01', '0.02', '0.05', '0.1', '0.2', '1e+09']:
        xs = byh[m][:seeds_complete]
        s_ = lambda k: float(sum(float(x[k]) for x in xs))
        tab.append(dict(margin=m, runs=len(xs), requests=s_('replan_requests'), completed=s_('completions'), mandatory=s_('mandatory'), optional=s_('optional'),
                        switches=s_('plan_switches'), route_changed=s_('switches_route_changed'), served=s_('served_on_time'), severity=s_('severity_served'),
                        dv=s_('delta_v_kms'), resp=float(np.mean([float(x['mean_response_h']) for x in xs]))))
    def paired(m1, m2, k):
        a = {x['seed']: float(x[k]) for x in byh[m1][:seeds_complete]}; bb = {x['seed']: float(x[k]) for x in byh[m2][:seeds_complete]}
        d = np.array([a[i] - bb[i] for i in sorted(set(a) & set(bb))]); nz = d[np.abs(d) > 1e-9]
        return dict(mean=float(d.mean()), higher=int((d > 1e-9).sum()), lower=int((d < -1e-9).sum()), p=float(wilcoxon(nz).pvalue) if len(nz) else None)
    e4h[sats] = dict(seeds=seeds_complete, table=tab, dv_005_vs_never=paired('0.05', '1e+09', 'delta_v_kms'),
                     resp_005_vs_never=paired('0.05', '1e+09', 'mean_response_h'), served_005_vs_never=paired('0.05', '1e+09', 'served_on_time'))
D['e4_hysteresis'] = e4h

# bug probe (pre-fix)
# Archived measurement made with the engine BEFORE the commitment fix (upstream commit
# a2c7aac, same harness, 50 satellites, 6 disturbances per hour). It cannot be regenerated
# from the fixed engine; see README.md.
import os
if not os.path.exists(R + 'e4/prefix_replanning.csv'):
    bp = []
else:
    bp = list(csv.DictReader(open(R + 'e4/prefix_replanning.csv')))
x5 = [x for x in bp if x['margin'] == '0.05']
if not x5: x5 = [dict(plan_switches=0, switches_mid_leg=0, uncharged_dv_kms=0, skipped_h=0, replan_requests=0, completions=0)]
D['bug_prefix'] = dict(runs=len(x5), switches=sum(int(x['plan_switches']) for x in x5), mid_leg=sum(int(x['switches_mid_leg']) for x in x5),
                       uncharged_dv=sum(float(x['uncharged_dv_kms']) for x in x5), skipped_h=sum(float(x['skipped_h']) for x in x5),
                       requests=sum(int(x['replan_requests']) for x in x5), completed=sum(int(x['completions']) for x in x5))
json.dump(D, open('results/numbers.json', 'w'), indent=1, default=float)
print(json.dumps({k: (v if k in ('e0', 'bug_prefix') else '...') for k, v in D.items()}, indent=1, default=float))

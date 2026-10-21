import csv, collections, numpy as np, sys
from scipy.stats import wilcoxon
for fn in sys.argv[1:]:
    r = list(csv.DictReader(open(fn)))
    by = collections.defaultdict(list)
    for x in r: by[x['margin']].append(x)
    print(fn, len(r), 'runs')
    print("margin   req  completed mandatory optional  switches route_changed mid_leg  served_on_time  severity  dv_kms  resp_h")
    base = None
    for m in ['0', '0.01', '0.02', '0.05', '0.1', '0.2', '1e+09']:
        xs = by.get(m, [])
        if not xs: continue
        s = lambda k: sum(float(x[k]) for x in xs)
        print(f"{m:>6} {s('replan_requests'):6.0f} {s('completions'):6.0f} {s('mandatory'):6.0f} {s('optional'):6.0f}   {s('plan_switches'):6.0f}  {s('switches_route_changed'):6.0f}  {s('switches_mid_leg'):6.0f}   {s('served_on_time'):6.0f}  {s('severity_served'):7.0f}  {s('delta_v_kms'):7.1f}  {np.mean([float(x['mean_response_h']) for x in xs]):5.2f}")
    # paired: margin 0 vs inf and 0.05 vs inf
    key = lambda x: x['seed']
    def paired(m1, m2, k):
        a = {key(x): float(x[k]) for x in by[m1]}; b = {key(x): float(x[k]) for x in by[m2]}
        ks = sorted(set(a) & set(b)); d = np.array([a[i] - b[i] for i in ks])
        nz = d[np.abs(d) > 1e-9]
        p = wilcoxon(nz).pvalue if len(nz) else float('nan')
        return np.mean(d), (d > 1e-9).sum(), (d < -1e-9).sum(), p
    for k in ['served_on_time', 'severity_served', 'delta_v_kms', 'plan_switches']:
        for m1 in ['0', '0.05']:
            md, up, dn, p = paired(m1, '1e+09', k)
            print(f"  {k:16s} margin {m1:>4} vs never-switch: mean diff {md:+.3f}, higher {up}, lower {dn}, Wilcoxon p={p:.3g}")

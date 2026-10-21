import csv, collections, numpy as np
from scipy.stats import wilcoxon, binomtest
rows = []
for fn in ['results/e4/dispatch_50.csv', 'results/e4/dispatch_100.csv']:
    try: rows += list(csv.DictReader(open(fn)))
    except FileNotFoundError: pass
key = lambda r: (r['satellites'], r['window_h'], r['cycle_s'], r['seed'])
by = collections.defaultdict(dict)
for r in rows: by[key(r)][r['solver']] = r
print("sats window cycle  n | served on time H  G | H>G H<G p_sign | severity H  G | dv H  G (km/s, median) | response h H G")
for sats in ['50', '100']:
    for w in ['12', '24', '48']:
        for c in ['0', '3600', '10800']:
            pairs = [v for k, v in by.items() if k[:3] == (sats, w, c) and len(v) == 2]
            if not pairs: continue
            H = lambda k: np.array([float(p['hungarian'][k]) for p in pairs]); G = lambda k: np.array([float(p['greedy'][k]) for p in pairs])
            sh, sg = H('served_on_time'), G('served_on_time')
            gt, lt = int((sh > sg).sum()), int((sh < sg).sum())
            p = binomtest(gt, gt + lt).pvalue if gt + lt else float('nan')
            print(f"{sats:>4} {w:>5}h {int(c)//3600:3d}h {len(pairs):3d} | {sh.sum():5.0f} {sg.sum():5.0f} | {gt:3d} {lt:3d} {p:8.2e} | {H('severity_served').sum():6.0f} {G('severity_served').sum():6.0f} | {np.median(H('delta_v_kms')):6.2f} {np.median(G('delta_v_kms')):6.2f} | {np.mean(H('mean_response_h')):5.2f} {np.mean(G('mean_response_h')):5.2f}")

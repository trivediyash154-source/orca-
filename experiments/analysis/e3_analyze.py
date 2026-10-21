import csv, collections, numpy as np
from scipy.sparse import csr_matrix
from scipy.sparse.csgraph import maximum_bipartite_matching
from scipy.stats import wilcoxon, binomtest

I = list(csv.DictReader(open('results/e3/instances.csv')))
cells = collections.defaultdict(list)
for c in csv.DictReader(open('results/e3/cells.csv')):
    cells[int(c['iid'])].append(c)

def max_matching(iid, n):
    rows, cols = [], []
    for c in cells[iid]:
        if c['feasible'] == '1':
            rows.append(int(c['row'])); cols.append(int(c['col']))
    if not rows: return 0
    m = csr_matrix((np.ones(len(rows)), (rows, cols)), shape=(n, n))
    match = maximum_bipartite_matching(m, perm_type='column')
    return int((match >= 0).sum())

print("size window  n  maxMatch(med)  greedy<max  hung<max  H>G  H=G  H<G  meanGap  signtest_p | equal-coverage: n  H<G_cost  H=G  H>G  median regret  wilcoxon_p")
summary = []
for win in ['none', '48h', '24h']:
    for size in ['2', '5', '10', '20', '30']:
        xs = [x for x in I if x['size'] == size and x['window'] == win]
        if not xs: continue
        n = int(size)
        mm = [max_matching(int(x['iid']), n) for x in xs]
        g = np.array([int(x['greedy_assigned']) for x in xs]); h = np.array([int(x['hung_assigned']) for x in xs])
        mm = np.array(mm)
        gt, eq, lt = int((h > g).sum()), int((h == g).sum()), int((h < g).sum())
        p = binomtest(gt, gt + lt).pvalue if gt + lt > 0 else float('nan')
        eqc = [(float(x['greedy_cost']), float(x['hung_cost'])) for x in xs if x['greedy_assigned'] == x['hung_assigned']]
        reg = np.array([gc - hc for gc, hc in eqc])
        better = int((reg > 1e-9).sum()); same = int((np.abs(reg) <= 1e-9).sum()); worse = int((reg < -1e-9).sum())
        try:
            wp = wilcoxon([a for a, b in eqc], [b for a, b in eqc], alternative='greater').pvalue if better + worse > 0 else float('nan')
        except ValueError:
            wp = float('nan')
        print(f"{size:>4} {win:>5} {len(xs):3d}  {np.median(mm):6.1f}       {int((g < mm).sum()):3d}        {int((h < mm).sum()):3d}    {gt:3d}  {eq:3d}  {lt:3d}  {np.mean(h - g):6.2f}  {p:9.2e} | {len(eqc):3d}  {better:3d}  {same:3d}  {worse:3d}  {np.median(reg) if len(reg) else float('nan'):8.4f}  {wp:9.2e}")
        summary.append(dict(size=size, window=win, n=len(xs), gt=gt, eq=eq, lt=lt, hung_below_max=int((h < mm).sum()), greedy_below_max=int((g < mm).sum()), eqcov=len(eqc), better=better, same=same, worse=worse))
# pooled
xs = I
g = np.array([int(x['greedy_assigned']) for x in xs]); h = np.array([int(x['hung_assigned']) for x in xs])
gt, lt = int((h > g).sum()), int((h < g).sum())
print(f"\nPooled: {len(xs)} instances; Hungarian covers more on {gt}, fewer on {lt}, equal on {len(xs)-gt-lt}; sign test p={binomtest(gt, gt+lt).pvalue:.2e}")
eqc = [(float(x['greedy_cost']), float(x['hung_cost'])) for x in xs if x['greedy_assigned'] == x['hung_assigned']]
reg = np.array([a - b for a, b in eqc])
print(f"Equal coverage: {len(eqc)} instances; Hungarian cheaper on {(reg>1e-9).sum()}, equal on {(np.abs(reg)<=1e-9).sum()}, dearer on {(reg<-1e-9).sum()}; Wilcoxon (greedy>hung) p={wilcoxon([a for a,b in eqc],[b for a,b in eqc], alternative='greater').pvalue:.2e}")
nontriv = [x for x in xs if int(x['size']) >= 5]
mmall = np.array([max_matching(int(x['iid']), int(x['size'])) for x in xs])
print(f"Hungarian below maximum matching: {(h < mmall).sum()} of {len(xs)}; greedy below maximum: {(g < mmall).sum()} of {len(xs)}")
ht = np.array([float(x['hung_ms']) for x in xs if x['size']=='30']); gtm = np.array([float(x['greedy_ms']) for x in xs if x['size']=='30'])
print(f"Solver time at 30x30: Hungarian median {np.median(ht):.3f} ms, greedy {np.median(gtm):.3f} ms")

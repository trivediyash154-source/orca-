"""Concatenates the three E2 runs into results/e2/queries.csv and fronts.csv with unique ids."""
import csv
qs, fs, off = [], [], 0
for part in ['small', '300', '500']:
    q = list(csv.DictReader(open(f'results/e2/q_{part}.csv')))
    f = list(csv.DictReader(open(f'results/e2/f_{part}.csv')))
    for r in q: r['qid'] = str(int(r['qid']) + off)
    for r in f: r['qid'] = str(int(r['qid']) + off)
    off += len(q); qs += q; fs += f
for name, rows in [('queries', qs), ('fronts', fs)]:
    with open(f'results/e2/{name}.csv', 'w', newline='') as h:
        w = csv.DictWriter(h, fieldnames=list(rows[0].keys())); w.writeheader(); w.writerows(rows)
print(len(qs), 'queries')

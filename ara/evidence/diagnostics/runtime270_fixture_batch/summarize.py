import json,statistics
from pathlib import Path
root=Path('/tmp/intrinsic-fixture-results-20260921')
pop={a:[] for a in ('before','after')}
for d in sorted(root.glob('[0-9][0-9]-*')):
    if (d/'raw-result.json').exists():pop[d.name.split('-')[1]].append((d,json.loads((d/'raw-result.json').read_text())))
assert all(len(v)==2 for v in pop.values()),'Incomplete population'
result={}
for scenario in pop['before'][0][1]['metrics']['build_time_ms']:
    row={}
    for arm,items in pop.items():
        vals=[r['metrics']['build_time_ms'][scenario]/1000 for _,r in items]
        counts=[r['diagnostics']['scenarios'][scenario]['compiler_invocations'] for _,r in items]
        row[arm]={'seconds':vals,'median_s':statistics.median(vals),'min_s':min(vals),'max_s':max(vals),'compiler_invocations':counts,'loadavg_before':[r['diagnostics']['scenarios'][scenario]['execution']['loadavg_before'] for _,r in items]}
    b,a=row['before']['median_s'],row['after']['median_s'];row.update(saved_s=b-a,saved_percent=100*(b-a)/b);result[scenario]=row
source_sets={}
for arm,items in pop.items():
    source_sets[arm]={}
    for s in ('fixture_header','mock_header'):
        sets=[{r['source'] for r in json.loads((d/f'{s}.hotspots.json').read_text())} for d,_ in items]
        assert sets[0]==sets[1],f'Unstable fanout: {arm} {s}'
        source_sets[arm][s]=sorted(sets[0])
removed={'tests/contract/runtime/Test.SandboxEditor'+n+'.cpp' for n in ('Models','SceneCommands','Visualization')}
assert removed <= set(source_sets['before']['mock_header'])
assert not removed & set(source_sets['after']['mock_header'])
owner_costs={}
for arm,items in pop.items():
    rows=[json.loads((d/'clean.hotspots.json').read_text()) for d,_ in items]
    sources=set.intersection(*[{r['source'] for r in rs} for rs in rows])
    owner_costs[arm]={s:statistics.median(sum(r['duration_ms'] for r in rs if r['source']==s)/1000 for rs in rows) for s in sorted(sources)}
out={'scope':'Local descriptive runtime-contract target comparison, not general/statistical claim','scenarios':result,'header_sources':source_sets,'clean_compiler_seconds_by_source':owner_costs}
(root/'summary.json').write_text(json.dumps(out,indent=2)+'\n')
for s,r in result.items():
    b,a=r['before'],r['after'];print(f"| {s} | {b['median_s']:.3f} ({b['min_s']:.3f}–{b['max_s']:.3f}) | {a['median_s']:.3f} ({a['min_s']:.3f}–{a['max_s']:.3f}) | {r['saved_s']:+.3f} | {r['saved_percent']:+.2f}% | {b['compiler_invocations']} → {a['compiler_invocations']} |")

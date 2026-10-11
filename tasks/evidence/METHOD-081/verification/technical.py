"""Pass/fail execution checks at final algorithm settings; no outcome metrics reported."""
from pathlib import Path
import importlib.util,json,subprocess,resource
HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('compare',HERE.parent/'compare.py');c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
folder=HERE/'technical';folder.mkdir(exist_ok=False)
records=[]
for variant,iters in [('points',7),('box',2)]:
 cell=('technical_'+variant,'candidate',variant,iters)
 args=c.command(cell,folder)[:-2] # --reference only reads an endpoint; no algorithm setting changes.
 with (folder/(variant+'.log')).open('w') as log:
  subprocess.run(args,cwd=c.ROOT,stdout=log,stderr=log,timeout=240,check=True,preexec_fn=c.limits)
 d=json.loads((folder/(cell[0]+'.json')).read_text())
 assert d['status']=='success' and d['iterations']==iters and d['nystrom_iterations']>0
 p=c.positions(folder/(cell[0]+'.bin'));assert p.shape==(100000,3)
 rows,stages=c.traces(folder/(variant+'.log'));assert len(rows)==iters and stages
 records.append({'variant':variant,'pass':True,'max_child_rss_kib':resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss})
 print('PASS:',variant,'final-size native path, finite full output, complete trace',flush=True)
(c.HERE/'technical.json').write_text(json.dumps(records,indent=2)+'\n')

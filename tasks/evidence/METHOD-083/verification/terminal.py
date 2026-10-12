"""Numeric-blind terminal validity test; no quality or timing outcome is interpreted."""
import concurrent.futures, hashlib, json, os, signal, subprocess, sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import comparison as c

folder=Path(__file__).resolve().parent/'terminal-raw'
folder.mkdir(exist_ok=False)
expected=hashlib.sha256((c.HERE/'preflight/manifest.txt').read_bytes()).hexdigest()
plan=[x for x in c.cells() if x['fixture']=='full' and x['seed']==1 and x['arm'] in ('baseline_points','adapted_union','upstream_nystrom') and not x['name'].endswith('repeat')]

def terminal(cell,slot):
 assert hashlib.sha256(subprocess.check_output(c.manifest_command(),cwd=c.ROOT)).hexdigest()==expected
 command=c.command(cell,folder,slot)
 (folder/(cell['name']+'.command.json')).write_text(json.dumps(command,indent=2)+'\n')
 with (folder/(cell['name']+'.stdout')).open('w') as stdout,(folder/(cell['name']+'.stderr')).open('w') as stderr:
  process=subprocess.Popen(command,cwd=c.ROOT,stdout=stdout,stderr=stderr,start_new_session=True,env=dict(os.environ,OMP_NUM_THREADS='8',OPENBLAS_NUM_THREADS='1'))
  try:
   code=process.wait(timeout=c.CELL_SECONDS+10)
   assert code==0,cell['name']+': no successful terminal output'
  finally:
   if process.poll() is None:os.killpg(process.pid,signal.SIGKILL)
   process.wait()
 d,trace,stages,positions=c.load(cell,folder,dict(kind='native_result',exit_code=0))
 assert d['status']=='success' and positions is not None and d['iterations']<=c.CAP
 print('PASS:',cell['arm'],'final-configuration terminal execution, finite endpoint, native counts and output schema',flush=True)

with concurrent.futures.ThreadPoolExecutor(max_workers=2) as executor:
 futures=[executor.submit(terminal,plan[i],i) for i in range(2)]
 for future in futures:future.result()
terminal(plan[2],0)
(folder/'sha256.json').write_text(json.dumps({p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(folder.iterdir())},indent=2)+'\n')
print('PASS: three executable terminal paths; scientific metrics not printed or interpreted')

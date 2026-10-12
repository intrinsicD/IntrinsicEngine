"""Exercise the full report with known endpoints and explicitly wrong results."""
import asyncio, hashlib, json, shutil, tempfile
from unittest.mock import patch
from pathlib import Path
import numpy as np
import comparison as c

def check():
 with tempfile.TemporaryDirectory(prefix='method083-check-') as temp:
  c.HERE=Path(temp)/'METHOD-083';c.HERE.mkdir();c.INPUT=Path(temp)/'input';c.INPUT.mkdir()
  c.N=200;c.CAP=64;c.OUT=c.HERE/'runs';c.OUT.mkdir();rawdir=Path(temp)/'METHOD-082/fixtures';rawdir.mkdir(parents=True)
  x=np.linspace(-1,1,c.N);source=np.column_stack([x,np.sin(3*x),np.cos(2*x)]).astype(np.float32).astype(float)
  truth=(source+[.3,-.2,.1]).astype(np.float32).astype(float);truth[1,0]=.2
  for f in ('full','partial'):
   for seed in (1,2,3):
    np.savez(rawdir/f'{f}_{seed}.npz',source=source,target=truth,truth=truth)
    np.savetxt(c.INPUT/f'{f}_{seed}_truth.txt',truth,fmt='%.17g')
  binding={f'{f}_{s}':dict(frame=dict(scale=1,target_mean=[0,0,0])) for f in ('full','partial') for s in (1,2,3)}
  (c.HERE/'input-binding.json').write_text(json.dumps(binding))
  def whole(mode):
   outcomes={}
   for cell in c.cells():
    name=cell['name'];up=cell['arm'].startswith('upstream');new=cell['arm'].startswith('adapted')
    p=truth.copy()
    if mode=='identity':p=source.copy()
    if mode=='collapse':p[:]=truth.mean(0)
    if mode=='wrongconsistent':p+=2
    if mode=='regression' and new and not cell['arm'].endswith('auto'):p+=.02
    if mode=='different_replay' and name.endswith('_repeat'):p+=.003
    cap=mode=='cap';count=c.CAP if cap else 31 if up else 3
    status='numerical_failure' if mode=='numerical' and new else 'success'
    outcomes[name]=dict(kind='native_result',exit_code=0 if status=='success' else 1)
    if up:
     (c.OUT/(name+'_info.txt')).write_text(f'loops\t{count}\nsigma\t0.1\nN_hat\t100\n')
     (c.OUT/(name+'_s.txt')).write_text('1\n');np.savetxt(c.OUT/(name+'_y.txt'),p,fmt='%.17g')
     (c.OUT/(name+'.stderr')).write_text(''.join(f'loop={i+1:03} acc.P=[Nystrom] N_hat=100 sigma=0.1 diff=0.0\n' for i in range(count)))
    else:
     policy=1 if cell['arm'].endswith('auto') else 5
     d=dict(fixture=cell['fixture'],seed=cell['seed'],points=c.N,threads=8,variant=c.VARIANTS[cell['arm'].split('_',1)[1]],landmarks=256,error_limit=.001,status=status,termination=('iteration_cap' if cap else 'converged') if status=='success' else 'none',iterations=count if status=='success' else 0,observed_iterations=count,nystrom_iterations=count if policy==5 else 0,sigma2=.01,scale=1,rms_to_truth=c.distances(p,truth)[0])
     (c.OUT/(name+'.json')).write_text(json.dumps(d))
     endpoint=c.OUT/(name+'.bin')
     if status=='success':p.astype('<f8').tofile(endpoint)
     else:endpoint.unlink(missing_ok=True)
     (c.OUT/(name+'.stderr')).write_text('CPD_STAGE expectation_step 0\n'+''.join(f'CPD_ITER {i} .01 1 1 100 {policy} .0001 100 {i+1} 0\n' for i in range(count)))
    for suffix in ('.png','-positions.png'):(c.OUT/(name+suffix)).write_bytes(b'cached plotting; checked separately')
   name=c.cells()[4]['name']
   if mode=='timeout':outcomes[name]=dict(kind='unevaluated',reason='native timeout')
   if mode=='not_started':
    outcomes[name]=dict(kind='unevaluated',reason='total budget exhausted');(c.OUT/(name+'.stderr')).unlink()
   if mode=='missing':(c.OUT/(name+'.bin')).unlink()
   if mode=='nonfinite':
    p=truth.copy();p[0,0]=np.nan;p.tofile(c.OUT/(name+'.bin'))
   if mode=='upstream_bad_trace':(c.OUT/(c.cells()[7]['name']+'.stderr')).write_text('')
   (c.OUT/'outcomes.json').write_text(json.dumps(outcomes))
   return c.report()
  good=whole('positive');assert good['adapted_convergence_and_auto']=='SUPPORTED' and all(good['replays'].values())
  assert good['adapted_union_vs_upstream']=='SUPPORTED' and all(x=='SUPPORTED' for x in good['truth_screen_by_arm'].values())
  for mode in ('cap','regression','numerical'):
   r=whole(mode);assert r['adapted_convergence_and_auto']=='REFUTED',mode
  for mode in ('identity','collapse','wrongconsistent'):
   r=whole(mode);assert r['adapted_convergence_and_auto']=='SUPPORTED' and all(x=='REFUTED' for x in r['truth_screen_by_arm'].values()),mode
  for mode in ('timeout','not_started'):
   r=whole(mode);assert not r['invalid_cells'] and r['adapted_convergence_and_auto']=='UNEVALUATED',mode
  for mode in ('missing','nonfinite','upstream_bad_trace'):
   assert whole(mode)['invalid_cells'],mode
  r=whole('different_replay');assert not all(r['replays'].values())
  whole('positive')
  for cell in (c.cells()[0],c.cells()[7]):
   for suffix in ('.png','-positions.png'):(c.OUT/(cell['name']+suffix)).unlink()
  c.report()
  for cell in (c.cells()[0],c.cells()[7]):
   for suffix in ('.png','-positions.png'):assert (c.OUT/(cell['name']+suffix)).read_bytes().startswith(b'\x89PNG')
  assert '<h1>' in (c.OUT/'report.html').read_text()
  # Mutation: remove the endpoint-agreement gate. A mismatched candidate must then escape it.
  original=c.quality;c.quality=lambda *args:True
  try:assert whole('regression')['adapted_convergence_and_auto']=='SUPPORTED'
  finally:c.quality=original
  # Execute the controller itself with deterministic native files and no native process.
  cached=c.OUT;original_cells=c.cells;plan=original_cells()[:2];c.WORKERS=1
  (c.HERE/'preflight').mkdir();(c.HERE/'reviews').mkdir()
  frozen=b'frozen control inputs';(c.HERE/'preflight/manifest.txt').write_bytes(frozen)
  digest=hashlib.sha256(frozen).hexdigest()
  (c.HERE/'reviews/approval.json').write_text(json.dumps(dict(digest=digest,codex_a='PASS',codex_b='PASS')))
  for mode in ('positive','invalid_output','changed_digest'):
   c.OUT=c.HERE/('controller-'+mode);calls=[]
   class Process:
    pid=987654321;returncode=0
    async def wait(self):return 0
   async def native(*command,**kwargs):
    name=command[1];calls.append(name)
    for suffix in ('.json','.bin','.stderr','.png','-positions.png'):
     shutil.copyfile(cached/(name+suffix),c.OUT/(name+suffix))
    if mode=='invalid_output':
     p=c.OUT/(name+'.json');d=json.loads(p.read_text());d['rms_to_truth']=999;p.write_text(json.dumps(d))
    return Process()
   with patch.object(c,'cells',lambda:plan),patch.object(c,'manifest_command',lambda:['manifest']),patch.object(c,'command',lambda cell,*args:['fake',cell['name']]),patch.object(c.subprocess,'check_output',return_value=b'changed' if mode=='changed_digest' else frozen),patch.object(c.asyncio,'create_subprocess_exec',side_effect=native):
    try:asyncio.run(c.run_async())
    except AssertionError:assert mode!='positive'
    else:assert mode=='positive'
   assert len(calls)==(2 if mode=='positive' else 1 if mode=='invalid_output' else 0)
   summary=json.loads((c.OUT/'summary.json').read_text());assert bool(summary['technical_errors'])==(mode!='positive')
 print('PASS: full report rejects no-op, collapse, wrong-but-consistent, cap, numerical failure, endpoint regression, missing/nonfinite data, malformed original trace and replay drift; censored/unstarted cells stay unevaluated; real engine/upstream figures render; agreement-gate mutation escapes the regression control as predicted; real controller starts valid cells and stops before the next native launch on changed digest or invalid output')

if __name__=='__main__':check()

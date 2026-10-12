"""Exercise the full report with known endpoints and explicitly wrong results."""
import asyncio, hashlib, json, shutil, tempfile
from unittest.mock import patch
from pathlib import Path
import numpy as np
import comparison as c

def check():
 binding=json.loads((c.HERE/'input-binding.json').read_text())
 for key,item in binding.items():
  frame=item['frame'];initial=np.loadtxt(c.INPUT/(key+'_source.txt'))*frame['scale']+frame['target_mean']
  with np.load(c.HERE.parent/'METHOD-082/fixtures'/(key+'.npz')) as data:
   assert not c.metrics(initial,data,initial)['truth_screen'],key
 with tempfile.TemporaryDirectory(prefix='method083-check-') as temp:
  c.HERE=Path(temp)/'METHOD-083';c.HERE.mkdir();c.INPUT=Path(temp)/'input';c.INPUT.mkdir()
  c.N=200;c.CAP=64;c.OUT=c.HERE/'runs';c.OUT.mkdir();rawdir=Path(temp)/'METHOD-082/fixtures';rawdir.mkdir(parents=True)
  x=np.linspace(-1,1,c.N);source=np.column_stack([x,np.sin(3*x),np.cos(2*x)]).astype(np.float32).astype(float)
  truth=(source+[.3,-.2,.1]+np.column_stack([.07*x*x,.04*np.cos(x),.03*x])).astype(np.float32).astype(float);truth[1,0]=.2
  frame=dict(scale=1.7,source_mean=source.mean(0).tolist(),target_mean=truth.mean(0).tolist())
  normalized=lambda p:((p-frame['target_mean'])/frame['scale']).astype(np.float32).astype(float)
  native_source=((source-frame['source_mean'])/frame['scale']).astype(np.float32).astype(float)
  for f in ('full','partial'):
   for seed in (1,2,3):
    np.savez(rawdir/f'{f}_{seed}.npz',source=source,target=truth,truth=truth)
    np.savetxt(c.INPUT/f'{f}_{seed}_truth.txt',normalized(truth),fmt='%.17g')
    np.savetxt(c.INPUT/f'{f}_{seed}_source.txt',native_source,fmt='%.17g')
  binding={f'{f}_{s}':dict(frame=frame) for f in ('full','partial') for s in (1,2,3)}
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
    p=native_source.copy() if mode=='native_noop' else normalized(p)
    cap=mode=='cap';count=c.CAP if cap else 31 if up else 3
    status='numerical_failure' if mode=='numerical' and new else 'success'
    outcomes[name]=dict(kind='native_result',exit_code=0 if status=='success' else 1)
    if up:
     sigma='.2' if mode=='different_final' and name.endswith('_repeat') else '.1'
     (c.OUT/(name+'_info.txt')).write_text(f'loops\t{count}\nsigma\t{sigma}\nN_hat\t100\n')
     (c.OUT/(name+'_s.txt')).write_text('1\n');np.savetxt(c.OUT/(name+'_y.txt'),p,fmt='%.17g')
     (c.OUT/(name+'.stderr')).write_text(''.join(f'loop={i+1:03} acc.P=[Nystrom] N_hat=100 sigma=0.1 diff=0.0\n' for i in range(count)))
    else:
     policy=1 if cell['arm'].endswith('auto') else 5
     d=dict(fixture=cell['fixture'],seed=cell['seed'],points=c.N,threads=8,variant=c.VARIANTS[cell['arm'].split('_',1)[1]],landmarks=256,error_limit=.001,status=status,termination=('iteration_cap' if cap else 'converged') if status=='success' else 'none',iterations=count if status=='success' else 0,observed_iterations=count,nystrom_iterations=count if policy==5 else 0,sigma2=.01,scale=1,rms_to_truth=c.distances(p,normalized(truth))[0])
     (c.OUT/(name+'.json')).write_text(json.dumps(d))
     endpoint=c.OUT/(name+'.bin')
     if status=='success':p.astype('<f8').tofile(endpoint)
     else:endpoint.unlink(missing_ok=True)
     flag=int(mode=='different_local' and name.endswith('_repeat'))
     (c.OUT/(name+'.stderr')).write_text('CPD_STAGE expectation_step 0\n'+''.join(f'CPD_ITER {i} .01 1 1 100 {policy} .0001 100 {i+1} {flag}\n' for i in range(count)))
    for suffix in ('.png','-positions.png','-stages.png'):(c.OUT/(name+suffix)).write_bytes(b'cached plotting; checked separately')
   name=c.cells()[4]['name']
   if mode=='timeout':outcomes[name]=dict(kind='unevaluated',reason='native timeout')
   if mode in ('auto_timeout','auto_timeout_failure'):
    auto=c.cells()[3]['name'];outcomes[auto]=dict(kind='unevaluated',reason='native timeout')
    if mode=='auto_timeout_failure':
     path=c.OUT/(name+'.json');d=json.loads(path.read_text());d['termination']='sigma_floor';path.write_text(json.dumps(d))
   if mode=='not_started':
    outcomes[name]=dict(kind='unevaluated',reason='total budget exhausted');(c.OUT/(name+'.stderr')).unlink()
   if mode=='missing':(c.OUT/(name+'.bin')).unlink()
   if mode=='nonfinite':
    p=truth.copy();p[0,0]=np.nan;p.tofile(c.OUT/(name+'.bin'))
   if mode in ('censored_original_empty','censored_original_prefix','censored_replay'):
    first=next(x['name'] for x in c.cells() if x['arm']=='adapted_union' and x['fixture']=='full' and x['seed']==1)
    censored=first+'_repeat' if mode=='censored_replay' else first
    outcomes[censored]=dict(kind='unevaluated',reason='native timeout')
    if mode=='censored_original_empty':(c.OUT/(censored+'.stderr')).write_text('')
   if mode=='upstream_bad_trace':(c.OUT/(c.cells()[7]['name']+'.stderr')).write_text('')
   if mode in ('upstream_nan_sigma','upstream_nan_mass'):
    path=c.OUT/(c.cells()[7]['name']+'_info.txt');text=path.read_text();path.write_text(text.replace('sigma\t.1','sigma\tnan') if mode.endswith('sigma') else text.replace('N_hat\t100','N_hat\tnan'))
   if mode=='upstream_nan_scale':(c.OUT/(c.cells()[7]['name']+'_s.txt')).write_text('nan\n')
   (c.OUT/'outcomes.json').write_text(json.dumps(outcomes))
   return c.report()
  good=whole('positive');assert good['adapted_convergence_and_auto']=='SUPPORTED' and all(good['replays'].values())
  assert good['adapted_union_vs_upstream']=='SUPPORTED' and all(x=='SUPPORTED' for x in good['truth_screen_by_arm'].values())
  for mode in ('cap','regression','numerical','auto_timeout_failure'):
   r=whole(mode);assert r['adapted_convergence_and_auto']=='REFUTED',mode
  for mode in ('identity','native_noop','collapse','wrongconsistent'):
   r=whole(mode);assert r['adapted_convergence_and_auto']=='SUPPORTED' and all(x=='REFUTED' for x in r['truth_screen_by_arm'].values()),mode
  for mode in ('timeout','auto_timeout','not_started'):
   r=whole(mode);assert not r['invalid_cells'] and r['adapted_convergence_and_auto']=='UNEVALUATED',mode
  for mode in ('missing','nonfinite','upstream_bad_trace','upstream_nan_sigma','upstream_nan_scale','upstream_nan_mass'):
   assert whole(mode)['invalid_cells'],mode
  for mode in ('censored_original_empty','censored_original_prefix','censored_replay'):
   r=whole(mode);assert r['replays']['full_1_adapted_union_repeat'] is None and not r['invalid_cells'],mode
  for mode in ('different_replay','different_final','different_local'):
   r=whole(mode);assert not all(r['replays'].values()),mode
  whole('positive')
  for cell in (c.cells()[0],c.cells()[7]):
   for suffix in ('.png','-positions.png'):(c.OUT/(cell['name']+suffix)).unlink()
  c.report()
  stage_image=c.OUT/(c.cells()[0]['name']+'-stages.png');stage_image.unlink();c.report()
  assert stage_image.read_bytes().startswith(b'\x89PNG')
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
 print('PASS: full report rejects raw/frame-aware no-op, collapse, wrong-but-consistent, cap, numerical failure, endpoint regression, missing/nonfinite data including upstream metadata, malformed original trace and replay endpoint/final-state/local-flag drift; censored original/replay pairs remain null, and censored/unstarted/missing-Auto comparisons stay unevaluated unless an observed failure refutes them; real engine/upstream/stage figures render; agreement-gate mutation escapes the regression control as predicted; real controller starts valid cells and stops before the next native launch on changed digest or invalid output')

if __name__=='__main__':check()

"""Known-answer adapter and one-update checks; no scientific endpoint results."""
import json, os, subprocess, tempfile
from pathlib import Path
import numpy as np
HERE=Path(__file__).resolve().parent
BIN=Path('/tmp/method083/bin');INPUT=Path('/tmp/method083/inputs')
env=dict(os.environ,OPENBLAS_NUM_THREADS='1',OMP_NUM_THREADS='1')
with tempfile.TemporaryDirectory(prefix='method083-adapter-') as temp:
 d=Path(temp)
 for key in ('source','target','truth'):
  a=np.loadtxt(INPUT/f'full_1_{key}.txt')[:64];np.savetxt(d/f'{key}.txt',a,fmt='%.17g')
 def command(arm,out,positions,truth=None):
  return [str(BIN/arm),'--fixture','full','--seed','1','--points','64','--source',str(d/'source.txt'),'--target',str(d/'target.txt'),'--truth',str(truth or d/'truth.txt'),'--variant','exact','--threads','1','--rank','0','--iterations','1','--out',str(out),'--positions',str(positions),'--trace','1']
 for arm in ('baseline','adapted'):
  r=subprocess.run(command(arm,d/f'{arm}.json',d/f'{arm}.bin'),capture_output=True,text=True,env=env,timeout=30)
  assert r.returncode==0 and json.loads((d/f'{arm}.json').read_text())['termination']=='iteration_cap',r.stderr
  a=np.fromfile(d/f'{arm}.bin',dtype='<f8').reshape(64,3);assert np.isfinite(a).all()
  for out,pos,message in [(Path('/dev/full'),d/'ok.bin','failed to write result JSON'),(d/'ok.json',Path('/dev/full'),'failed to write positions')]:
   fail=subprocess.run(command(arm,out,pos),capture_output=True,text=True,env=env,timeout=30)
   assert fail.returncode==2 and message in fail.stderr
  (d/'invalid.txt').write_text('0 0 0 trailing\n')
  fail=subprocess.run(command(arm,d/'bad.json',d/'bad.bin',d/'invalid.txt'),capture_output=True,text=True,env=env,timeout=30)
  assert fail.returncode==2 and 'invalid point row' in fail.stderr
  np.savetxt(d/'swapped.txt',np.ones((64,3))*9,fmt='%.17g')
  r=subprocess.run(command(arm,d/'swap.json',d/'swap.bin',d/'swapped.txt'),capture_output=True,text=True,env=env,timeout=30)
  assert r.returncode==0 and (d/'swap.bin').read_bytes()==(d/f'{arm}.bin').read_bytes(),'truth entered the solver'
 assert (d/'baseline.bin').read_bytes()==(d/'adapted.bin').read_bytes(),'inactive mechanism changes exact update'
 cmd=[str(BIN/'upstream'),'-x',str(d/'target.txt'),'-y',str(d/'source.txt'),'-o',str(d/'original_'),'-w','0.1','-b','1','-l','2','-g','1','-K','0','-J','0','-n','1','-N','1','-c','0.00001','-r','1','-u','n','-T','srn','-h','-s','ytT']
 r=subprocess.run(cmd,capture_output=True,text=True,env=env,timeout=30)
 assert r.returncode==0,r.stdout+r.stderr
 original=np.loadtxt(d/'original_y.txt');assert original.shape==(64,3) and np.isfinite(original).all()
 # Both exact, full-rank Bayesian update equations share this known one-step contract.
 # Upstream writes six decimal places, so tolerate its final coordinate quantization.
 assert np.max(np.abs(original-np.fromfile(d/'adapted.bin',dtype='<f8').reshape(64,3))) <= 5e-6,'full-rank one-update mismatch'
print('PASS: both adapters reject failed JSON/binary writes and malformed rows; truth cannot change solver output; exact old/adapted update bit-identical; upstream full-rank update agrees within its output precision budget')

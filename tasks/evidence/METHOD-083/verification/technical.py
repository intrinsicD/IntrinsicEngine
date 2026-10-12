"""Final-size/config process-path check. Stop on the first completed-step marker; do not inspect metrics."""
import os, selectors, signal, subprocess, sys, tempfile, time
from pathlib import Path
import psutil
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE.parent))
import comparison as c
with tempfile.TemporaryDirectory(prefix='method083-technical-') as temp:
 folder=Path(temp)
 # Every native configuration is checked on the largest/full input, plus partial union/upstream.
 plan=[x for x in c.cells() if x['fixture']=='full' and x['seed']==1 and not x['name'].endswith('repeat')]
 plan += [x for x in c.cells() if x['fixture']=='partial' and x['seed']==1 and x['arm'] in ('adapted_union','upstream_nystrom') and not x['name'].endswith('repeat')]
 for cell in plan:
  cmd=c.command(cell,folder); upstream=cell['arm'].startswith('upstream')
  with (folder/(cell['name']+'.stdout')).open('w') as output:
   p=subprocess.Popen(cmd,stdout=output,stderr=subprocess.PIPE,bufsize=0,start_new_session=True,env=dict(os.environ,OMP_NUM_THREADS='8',OPENBLAS_NUM_THREADS='1'))
   selector=selectors.DefaultSelector();selector.register(p.stderr,selectors.EVENT_READ)
   buffer=b'';seen=False;peak=0;deadline=time.monotonic()+120
   try:
    while time.monotonic()<deadline and p.poll() is None:
     try:
      processes=[psutil.Process(p.pid)]+psutil.Process(p.pid).children(recursive=True)
      peak=max(peak,sum(q.memory_info().rss for q in processes if q.is_running()))
     except (psutil.NoSuchProcess,psutil.AccessDenied):pass
     for key,_ in selector.select(.1):
      chunk=os.read(key.fd,65536)
      if not chunk:continue
      buffer+=chunk
      if (b'loop=001' if upstream else b'CPD_ITER 0 ') in buffer:
       seen=True;break
     if seen:break
    assert seen and peak>0 and peak<8*1024**3,cell['name']
    print('PASS:',cell['name'],'first completed native step; memory bound checked',flush=True)
   finally:
    if p.poll() is None:os.killpg(p.pid,signal.SIGKILL)
    p.wait();selector.close();p.stderr.close()
print('PASS: all final configurations execute; numerical outputs were not treated as results')

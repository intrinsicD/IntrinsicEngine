import subprocess,json,math,resource
from pathlib import Path
def limit_memory():
 resource.setrlimit(resource.RLIMIT_AS,(8<<30,8<<30))
out=Path('/tmp/cpd-nystrom-usage-20261011/technical-capped')
out.mkdir(exist_ok=False)
for name,landmarks,error in [('floor',4096,.001),('negative',2,1e300)]:
 args=['taskset','-c','0-7','/tmp/cpd-nystrom-usage-20261011/bin/baseline','--fixture','full','--seed','1','--points','100000','--threads','8','--iterations','20','--trace','1','--variant','points/fixed/random','--landmarks',str(landmarks),'--error-limit',str(error),'--out',str(out/(name+'.json')),'--positions',str(out/(name+'.bin')),'--reference','/tmp/cpd-nystrom-usage-20261011/technical/exact.bin']
 with (out/(name+'.stdout')).open('w') as stdout,(out/(name+'.stderr')).open('w') as stderr:
  subprocess.run(args,stdout=stdout,stderr=stderr,check=True,timeout=300,preexec_fn=limit_memory)
 row=json.loads((out/(name+'.json')).read_text())
 assert row['status']=='success' and 0<row['observed_iterations']<=20
 assert (out/(name+'.bin')).stat().st_size in (24*100000,32*100000)
 for line in (out/(name+'.stderr')).read_text().splitlines():
  if line.startswith('CPD_ITER '):assert all(math.isfinite(float(x)) for x in line.split()[1:])
 print(name+': PASS with 8 GiB RLIMIT_AS and 300s wall limit; technical execution, complete positions, finite traces; no efficacy or timing values inspected',flush=True)

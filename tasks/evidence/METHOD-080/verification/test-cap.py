"""Known-answer maximum three-attempt exact-sample workload; budget is pass/fail only."""
import importlib.util, math, resource, signal, time
from pathlib import Path
import numpy as np
source=Path(__file__).resolve().parents[1]/'census.py'
spec=importlib.util.spec_from_file_location('census',source);c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
c.limits()
def expired(signum,frame):raise TimeoutError('maximum exact-row workload exceeded 180 seconds')
signal.signal(signal.SIGALRM,expired);signal.alarm(180)
y=np.zeros((100000,3));y[:,0]=np.arange(100000)%10;lw=np.zeros(100000)
answers=[math.log(10000*sum(math.exp(-((i-k)**2)/2) for k in range(10))) for i in range(10)]
for i in range(3*(1024+2+256)):
    assert abs(float(c.exact([i%10,0,0],y,lw,1))-answers[i%10])<1e-13
signal.alarm(0)
print('PASS: maximum three-attempt exact-sample workload within 180s and 8 GiB; every analytic sum matched')
print('MEASURED: cap peak_rss_kib='+str(resource.getrusage(resource.RUSAGE_SELF).ru_maxrss))

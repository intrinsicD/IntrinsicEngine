import json,resource,subprocess
from pathlib import Path
import numpy as np
root=Path.cwd();out=root/"tasks/evidence/METHOD-082/verification"
def limits():resource.setrlimit(resource.RLIMIT_AS,(3*1024**3,)*2)
for fixture in ("full","partial"):
 name="technical-"+fixture
 cmd=["taskset","-c","0-7","/tmp/cpd-nystrom-repair-20261011/bin/candidate","--fixture",fixture,"--seed","1","--points","100000","--threads","8","--iterations","5000","--trace","1","--variant","points/fixed/random","--landmarks","256","--error-limit","0.001","--out",str(out/(name+".json")),"--positions",str(out/(name+".bin"))]
 (out/(name+".command.json")).write_text(json.dumps(cmd,indent=2))
 with (out/(name+".stdout")).open("w") as so,(out/(name+".stderr")).open("w") as se:
  subprocess.run(cmd,check=True,stdout=so,stderr=se,timeout=900,preexec_fn=limits)
 d=json.loads((out/(name+".json")).read_text());a=np.fromfile(out/(name+".bin"),dtype="<f8")
 assert d["status"]=="success" and d["termination"] in ("converged","iteration_cap","sigma_floor")
 assert 0<d["iterations"]<=5000 and a.size==300000 and np.isfinite(a).all()
 assert len([x for x in (out/(name+".stderr")).read_text().splitlines() if x.startswith("CPD_ITER ")])==d["iterations"]
 print("PASS:",fixture,"final-configuration real path; complete finite result and consistent trace (values not inspected)",flush=True)
print("PASS: both technical paths; not scientific evidence",flush=True)

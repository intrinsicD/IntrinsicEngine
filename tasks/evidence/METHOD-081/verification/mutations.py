"""Known-answer regression mutations; always restore production source."""
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[4]
HERE=Path(__file__).resolve().parent
source=ROOT/'src/geometry/Geometry.Registration.CoherentPointDrift.EStep.cpp'
original=source.read_text()
changes=[
 ('repair-budget','if (spent + repairCost >= 0.5 * double(n) * double(m)) return false;', 'if (false) return false;', 'NystromRepairsStillRespectAccuracyAndCostGuards'),
 ('sampled-accuracy','return sampledError <= errorLimit;', 'return true;', 'NystromRepairsStillRespectAccuracyAndCostGuards'),
 ('stable-repaired-sample','if (repair[j] != 0u) continue; // ExactRow already used a stable exponent shift.', '// removed repaired-sample bypass', 'NystromRepairsInvalidRowsWithAllWeightedStatistics'),
 ('exact-pxy','out.PXy[i] += p[k] * y[k];','// removed exact PY accumulation','NystromRepairsInvalidRowsWithAllWeightedStatistics'),
]
try:
 for name,old,new,test in changes:
  assert original.count(old)==1,(name,original.count(old))
  source.write_text(original.replace(old,new))
  with (HERE/(name+'-build.log')).open('w') as log:
   subprocess.run(['cmake','--build','--preset','ci','--target','IntrinsicGeometryRegistrationSlowTests'],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=240)
  with (HERE/(name+'-test.log')).open('w') as log:
   result=subprocess.run(['ctest','--test-dir','build/ci','--output-on-failure','--timeout','60','-R','^CoherentPointDriftAccelerated\\.'+test+'$'],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,timeout=75)
  assert result.returncode!=0,name+' escaped'
  print('PASS: regression rejects',name,'mutation',flush=True)
finally:
 source.write_text(original)
 with (HERE/'restored-build.log').open('w') as log:
  subprocess.run(['cmake','--build','--preset','ci','--target','IntrinsicGeometryRegistrationSlowTests'],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=240)

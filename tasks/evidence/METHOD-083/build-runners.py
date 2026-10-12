"""Build one frozen adapter against old/adapted geometry, restoring every staged source."""
import hashlib, json, subprocess, sys
from pathlib import Path
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
BIN=Path('/tmp/method083/bin'); BIN.mkdir(parents=True,exist_ok=True)
arm=sys.argv[1]; assert arm in ('baseline','adapted')
runner=ROOT/'benchmarks/runners/CoherentPointDriftLandmarkVariantsRunner.cpp'
paths=[runner]+[ROOT/('src/geometry/Geometry.Registration.CoherentPointDrift'+suffix) for suffix in ('.cpp','.cppm','.EStep.cpp','.EStep.cppm')]
saved={p:p.read_bytes() for p in paths}
source=(HERE/'diagnostic-runner.cpp').read_bytes()
try:
 if arm=='baseline':
  for p in paths[1:]: p.write_bytes(subprocess.check_output(['git','show','ff5496965:'+str(p.relative_to(ROOT))],cwd=ROOT))
 runner.write_bytes((b'#define METHOD083 1\n' if arm=='adapted' else b'')+source)
 with (HERE/'verification'/f'{arm}-final-build.log').open('w') as out:
  subprocess.run(['cmake','--build','--preset','ci-release','--target','IntrinsicCoherentPointDriftLandmarkVariants'],cwd=ROOT,stdout=out,stderr=subprocess.STDOUT,check=True,timeout=600)
 binary=ROOT/'build/ci-release/bin/IntrinsicCoherentPointDriftLandmarkVariants'
 (BIN/arm).write_bytes(binary.read_bytes());(BIN/arm).chmod(0o755)
 binding=dict(arm=arm,runner_sha256=hashlib.sha256(source).hexdigest(),binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),geometry={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths[1:]})
 (HERE/'verification'/f'{arm}-binding.json').write_text(json.dumps(binding,indent=2)+'\n')
 print('BUILT',arm,binding['binary_sha256'])
finally:
 for p,b in saved.items(): p.write_bytes(b)

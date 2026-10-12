"""Deterministic mutation checks, restoring the shared implementation even on failure."""
import subprocess
from pathlib import Path
ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent
p = ROOT / 'src/geometry/Geometry.Registration.CoherentPointDrift.EStep.cpp'
original = p.read_bytes()
mutations = [
 ('union', 'if (settings.NystromSource == NystromLandmarkSource::UnionRandom)',
  'if (false && settings.NystromSource == NystromLandmarkSource::UnionRandom)',
  'UnionRandomUsesOnePermutationAndFollowsMovingIdentities'),
 ('switch', 'if (nystromLocal) used = EStepPolicy::Truncated;',
  'if (false && nystromLocal) used = EStepPolicy::Truncated;',
  'NystromLocalTransitionUsesSigmaAndValidatesBeforeSwitch'),
 ('repair', 'AddExactRows(moved, sigma2, logC, exactRows, threads, out);',
  '/* mutation: omitted complete row repair */', 'NystromRepairsInvalidRowsWithAllWeightedStatistics'),
]
try:
 for name, old, new, case in mutations:
  source = original.decode(); assert source.count(old) == 1
  p.write_text(source.replace(old, new))
  with (HERE / f'mutation-{name}-build.log').open('w') as f:
   subprocess.run(['cmake', '--build', '--preset', 'ci', '--target', 'IntrinsicGeometryRegistrationSlowTests'], cwd=ROOT, stdout=f, stderr=subprocess.STDOUT, check=True, timeout=240)
  result = subprocess.run(['ctest', '--test-dir', 'build/ci', '--output-on-failure', '--timeout', '60', '-R', '^CoherentPointDriftAccelerated.'+case+'$'], cwd=ROOT, capture_output=True, text=True, timeout=90)
  (HERE / f'mutation-{name}-test.log').write_text(result.stdout + result.stderr)
  assert result.returncode != 0 and '***Failed' in result.stdout and '0% tests passed' in result.stdout, name
  print(f'PASS: {name} mutation rejected by its contract test', flush=True)
finally:
 p.write_bytes(original)
 with (HERE / 'mutation-restored-build.log').open('w') as f:
  subprocess.run(['cmake', '--build', '--preset', 'ci', '--target', 'IntrinsicGeometryRegistrationSlowTests'], cwd=ROOT, stdout=f, stderr=subprocess.STDOUT, check=True, timeout=240)
 with (HERE / 'mutation-restored-test.log').open('w') as f:
  subprocess.run(['ctest', '--test-dir', 'build/ci', '--output-on-failure', '--timeout', '60', '-R', '^CoherentPointDriftAccelerated.(NystromRegularizesDependentLandmarks|UnionRandomUsesOnePermutationAndFollowsMovingIdentities|NystromLocalTransitionUsesSigmaAndValidatesBeforeSwitch|NystromRepairsInvalidRowsWithAllWeightedStatistics)$'], cwd=ROOT, stdout=f, stderr=subprocess.STDOUT, check=True, timeout=90)

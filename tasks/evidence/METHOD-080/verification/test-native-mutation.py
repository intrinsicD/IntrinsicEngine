"""Remove the all-row collection mechanism, verify its control fails, restore/build source."""
import importlib.util, os, shutil, subprocess
from pathlib import Path
source=Path(__file__).resolve().parents[1]/'census.py'
spec=importlib.util.spec_from_file_location('census',source);c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
native=c.ROOT/'src/geometry/Geometry.Registration.CoherentPointDrift.EStep.cpp'
original=native.read_bytes();old=b'if (capturePath) continue;';assert original.count(old)==1
out=Path('/tmp/cpd-nystrom-census-20261011/mutation');out.mkdir(exist_ok=False)
build=['cmake','--build','--preset','ci-release','--target','IntrinsicCoherentPointDriftLandmarkVariants']
try:
    native.write_bytes(original.replace(old,b'if (capturePath) return;'))
    with (out/'build.log').open('w') as log:subprocess.run(build,cwd=c.ROOT,stdout=log,stderr=subprocess.STDOUT,timeout=240,check=True)
    binary=out/'native';shutil.copy2(c.ROOT/'build/ci-release/bin/IntrinsicCoherentPointDriftLandmarkVariants',binary)
    env=dict(os.environ,INTRINSIC_NYSTROM_CAPTURE=str(out/'box_1.snapshot'),OPENBLAS_NUM_THREADS='1',OMP_NUM_THREADS='1')
    with (out/'box_1.stdout').open('w') as stdout,(out/'box_1.stderr').open('w') as stderr:
        subprocess.run(c.native_command(c.PLAN[1],out,binary),env=env,stdout=stdout,stderr=stderr,preexec_fn=c.limits,timeout=90,check=True)
    assert (out/'box_1.bin').read_bytes()==(out.parent/'technical/box_1-baseline.bin').read_bytes()
    path=out/'box_1.snapshot.draw1.L512.rows'
    try:c.rows(path)
    except AssertionError as error:assert 'unvisited' in str(error)
    else:raise AssertionError('early-return mutation escaped full-row gate')
    print('PASS: early-return mutant preserves rejection/endpoint but fails completeness (later same-chunk rows unvisited)',flush=True)
finally:
    native.write_bytes(original)
    with (out/'restore-build.log').open('w') as log:subprocess.run(build,cwd=c.ROOT,stdout=log,stderr=subprocess.STDOUT,timeout=240,check=True)
    assert native.read_bytes()==original
    assert (c.ROOT/'build/ci-release/bin/IntrinsicCoherentPointDriftLandmarkVariants').read_bytes()==c.BIN.read_bytes()
    print('PASS: exact original probe source and binary restored',flush=True)

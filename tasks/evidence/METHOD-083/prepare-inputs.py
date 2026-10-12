"""Export common float coordinates from the frozen METHOD-082 fixtures; no solver runs."""
import hashlib, json
from pathlib import Path
import numpy as np
HERE=Path(__file__).resolve().parent
DEST=Path('/tmp/method083/inputs')

def normalized(data):
    source,target,truth=(np.asarray(data[k],dtype=np.float32).astype(float) for k in ('source','target','truth'))
    sy,sx=source.mean(0),target.mean(0)
    scale=max(np.sqrt(np.mean(np.sum((source-sy)**2,axis=1))),np.sqrt(np.mean(np.sum((target-sx)**2,axis=1))))
    assert np.isfinite(scale) and scale>0
    sets=dict(source=((source-sy)/scale).astype(np.float32),target=((target-sx)/scale).astype(np.float32),truth=((truth-sx)/scale).astype(np.float32))
    for k,offset in [('source',sy),('target',sx),('truth',sx)]:
        expected=dict(source=source,target=target,truth=truth)[k]
        assert np.max(np.abs(sets[k].astype(float)*scale+offset-expected)) < 1e-6
    return sets,dict(source_mean=sy.tolist(),target_mean=sx.tolist(),scale=float(scale))

if __name__=='__main__':
    DEST.mkdir(parents=True,exist_ok=True)
    metadata={}
    for fixture in ('full','partial'):
        for seed in (1,2,3):
            key=f'{fixture}_{seed}'; src=HERE.parent/'METHOD-082/fixtures'/f'{key}.npz'
            with np.load(src) as raw: sets,frame=normalized(raw)
            metadata[key]=dict(frame=frame,original_sha256=hashlib.sha256(src.read_bytes()).hexdigest(),files={})
            for label,points in sets.items():
                path=DEST/f'{key}_{label}.txt';np.savetxt(path,points.astype(float),fmt='%.17g',delimiter='\t')
                # These decimals recover exactly the float inputs used by the engine; upstream reads the same doubles.
                assert np.array_equal(np.loadtxt(path),points.astype(float))
                metadata[key]['files'][label]=dict(path=str(path),count=len(points),sha256=hashlib.sha256(path.read_bytes()).hexdigest())
    (HERE/'input-binding.json').write_text(json.dumps(metadata,indent=2)+'\n')
    print('PASS: six fixtures exported with separate centers, shared RMS-radius scale, exact float serialization and inverse-frame assertions')

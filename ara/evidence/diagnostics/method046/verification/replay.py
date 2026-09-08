from pathlib import Path
import json
import sys
import numpy as np
sys.path.insert(0, str(Path('tools/diagnostics/atlas').resolve()))
import boundary_refine as br
mesh = sys.argv[1]
root = Path('build/method046/verified')/mesh
records = json.loads((root/'results.json').read_text())
scores = np.load(root/'scores.npz')
for arm in records['arms']:
    name = arm['arm']; p = root/name; z = np.load(p/'unpacked.npz')
    key = {'reference':'combined', 'length':'combined', 'native':'soft', 'curves':'combined', 'shuffled':'shuffled'}[name]
    result = br.execute(z['vertices'], z['faces'], z['region_labels'],
                        np.load(root/'reference/unpacked.npz')['labels'], scores[key],
                        hard=scores['hard'], beta=arm['beta'], rounds=arm['rounds'])
    for key in ('labels', 'region_labels', 'chart_uv', 'corner_uv'):
        np.testing.assert_array_equal(z[key], result[key])
    np.testing.assert_array_equal(np.load(p/'moves.npz')['labels'], np.stack(result['stages']))
    raw = json.loads((p/'unpacked.json').read_text())
    raw['events'] = result['events']; raw['metrics'] = result['metrics']
    (p/'unpacked.json').write_text(json.dumps(raw, indent=2, allow_nan=False)+'\n')
    print(mesh, name, 'exact labels, UVs and accepted stages unchanged; final diagnostics regenerated', flush=True)

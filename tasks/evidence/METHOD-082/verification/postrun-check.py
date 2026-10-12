"""Known-answer coverage regression: both censored traces must be reported without endpoints."""
import hashlib
import re
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
ROOT = HERE.parents[2]
report = HERE / 'postrun/report.html'
assert report.exists(), 'missing interrupted-trace appendix'
body = report.read_text()
for arm, steps in [('baseline', 3726), ('candidate', 3728)]:
    name = f'partial_2_{arm}_box'
    assert f'id="{name}"' in body and f'{steps} completed M-steps' in body
    assert f'<img src="{name}.png"' in body
    assert not (HERE / 'runs' / (name + '.json')).exists()
    assert not (HERE / 'runs' / (name + '.bin')).exists()
assert 'Endpoints and before/after positions unavailable' in body
assert 'Native convergence status unavailable' in body
assert 'INCOMPLETE / UNEVALUATED' in body and 'P1 and P2: refuted' in body
assert '143' in body and 'unknown cause' in body
for url in re.findall(r'(?:src|href)="([^"]+)"', body):
    assert (report.parent / url.split('#')[0]).is_file(), url
    if url.endswith('.png'):
        assert (report.parent / url).read_bytes().startswith(b'\x89PNG\r\n\x1a\n')
manifest = HERE / 'reviews/raw-sha256.txt'
assert hashlib.sha256(manifest.read_bytes()).hexdigest() == '7d068d3dec21450834cd82ccc906be6a97f0a6dfce0f2232dc0e8fa19b25731d'
lines = manifest.read_text().splitlines()
assert len(lines) == 233
for line in lines:
    digest, path = line.split('  ', 1)
    assert hashlib.sha256((ROOT / path).read_bytes()).hexdigest() == digest, path
print('PASS: two interrupted traces, unavailable endpoints, bound verdicts, valid links/figures, all233 original hashes unchanged')

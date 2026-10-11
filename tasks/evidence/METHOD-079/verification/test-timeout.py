"""Exercise real alarm -> child termination -> failure-before-report in the frozen driver."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import signal
import tempfile
from unittest.mock import patch
spec=importlib.util.spec_from_file_location('d',Path(__file__).resolve().parents[1]/'diagnose.py');d=importlib.util.module_from_spec(spec);spec.loader.exec_module(d)
with tempfile.TemporaryDirectory() as directory:
    root=Path(directory);d.HERE=root;d.OUT=root/'runs';d.PACKET=root/'packet';d.PACKET.mkdir();(root/'reviews').mkdir()
    frozen=b'known-answer-timeout';(d.PACKET/'manifest.txt').write_bytes(frozen)
    (root/'reviews/approvals.json').write_text(json.dumps({'digest':hashlib.sha256(frozen).hexdigest(),'claude':'PASS','codex':'PASS'}))
    d.MANIFEST=['/usr/bin/printf',frozen.decode()];pid=root/'pid'
    d.native_command=lambda cell,folder:['/usr/bin/python3','-c',f'import os,time;open({str(pid)!r},"w").write(str(os.getpid()));time.sleep(60)']
    def report():
        error=json.loads((d.OUT/'failure.json').read_text());assert 'TimeoutError' in error['error']
        (d.OUT/'report.html').write_text('UNEVALUATED: '+error['error'])
    d.report=report;alarm=signal.alarm
    with patch.object(signal,'alarm',side_effect=lambda seconds:alarm(1 if seconds else 0)):
        try:d.run();raise AssertionError('timeout missing')
        except TimeoutError:pass
    assert 'UNEVALUATED' in (d.OUT/'report.html').read_text()
    try:os.kill(int(pid.read_text()),0)
    except ProcessLookupError:pass
    else:raise AssertionError('child survives timeout')
print('PASS: real alarm kills child; failure record precedes report; timeout remains unevaluated')

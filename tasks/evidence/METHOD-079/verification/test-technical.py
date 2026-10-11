"""End-to-end assertions only. Numerical diagnostics are not read as research outcomes."""
import importlib.util
import json
import math
import os
from pathlib import Path
import struct
import subprocess
spec=importlib.util.spec_from_file_location('d',Path(__file__).resolve().parents[1]/'diagnose.py')
d=importlib.util.module_from_spec(spec);spec.loader.exec_module(d)
out=Path('/tmp/cpd-nystrom-row-20261011/technical')
for cell in d.PLAN[:2]:
    name=cell[0];stem=name.split('_')[0]
    env=dict(os.environ,INTRINSIC_NYSTROM_CAPTURE=str(out/(name+'.snapshot')))
    with (out/(name+'.stdout')).open('w') as stdout,(out/(name+'.stderr')).open('w') as stderr:
        r=subprocess.run(d.native_command(cell,out),env=env,stdout=stdout,stderr=stderr,preexec_fn=d.limits,timeout=90)
    assert r.returncode==0
    (out/(name+'.native.exit.json')).write_text(json.dumps({'returncode':0}))
    j=json.loads((out/(name+'.json')).read_text());base=json.loads((out/(stem+'-baseline.json')).read_text())
    assert (out/(name+'.bin')).read_bytes()==(out/(stem+'-baseline.bin')).read_bytes()
    for key in ['status','termination','iterations','nystrom_iterations','sigma2']: assert j[key]==base[key]
    assert len(d.previous.traces(out/(name+'.stderr'))[0])==cell[2]
    assert (out/(name+'.snapshot')).exists()
    with (out/(name+'.analysis.tsv')).open('w') as stdout,(out/(name+'.analysis.stderr')).open('w') as stderr:
        r=subprocess.run(d.analysis_command(cell,out),stdout=stdout,stderr=stderr,preexec_fn=d.limits,timeout=120)
    assert r.returncode==0,(out/(name+'.analysis.stderr')).read_text()
    (out/(name+'.analysis.exit.json')).write_text(json.dumps({'returncode':0}))
    m=d.metrics(out/(name+'.analysis.tsv'));assert m['native_replay_bitwise']==1
    assert (out/(name+'.sources.bin')).stat().st_size==2400000
    raw=bytearray((out/(name+'.snapshot')).read_bytes());_,l,n,_,_=struct.unpack_from('<5Q',raw)
    offset=40+8*(7+7*l+4*n+2*l*l);struct.pack_into('<d',raw,offset,struct.unpack_from('<d',raw,offset)[0]+1)
    corrupt=out/(name+'-corrupt.snapshot');corrupt.write_bytes(raw)
    r=subprocess.run([str(d.BINS/'analyze'),str(corrupt),str(out/'invalid')],capture_output=True,preexec_fn=d.limits,timeout=30)
    assert r.returncode!=0 and b'replay mismatch' in r.stderr
    print('PASS:',name,'native parity, finite state, captured operands, independent replay, corruption rejection',flush=True)
d.report(out)
assert (out/'report.html').exists() and len(list(out.glob('*-diagnostic.png')))==2
(out/'failure.json').write_text(json.dumps({'cell':'known-answer-control','error':'synthetic timeout'}))
d.report(out)
assert 'UNEVALUATED: technical failure' in (out/'report.html').read_text()
assert '<b>Negative sum persists' not in (out/'report.html').read_text()
assert 'INCOMPLETE: plan completion not recorded.' in (out/'report.html').read_text()
(out/'failure.json').unlink()
fake={k:-1. for k in ['native_sum','native_g_extended_dot','native_factors_extended_sum','native_gram_extended_eigen_sum','rebuilt_matched_rank_sum','rebuilt_sum']}
fake.update(rebuilt_accumulation_scale=1e-12,matched_accumulation_scale=1e-12)
assert d.verdict(fake).startswith('Negative sum persists')
fake['rebuilt_accumulation_scale']=2
assert d.verdict(fake).startswith('UNRESOLVED')
fake['rebuilt_accumulation_scale']=1e-12
fake['rebuilt_sum']=1
assert d.verdict(fake).startswith('UNRESOLVED')
print('PASS: full report path, fail-closed failure banner and sign-disagreement classification; no efficacy values inspected')

optimized=subprocess.run(['/usr/bin/python3',str(d.HERE/'diagnose.py'),'run'],env=dict(os.environ,PYTHONOPTIMIZE='1'),capture_output=True,text=True,timeout=10)
assert optimized.returncode!=0 and 'Optimized Python is forbidden' in optimized.stderr
print('PASS: optimized Python refuses execution before any start')
(out/'points_1.analysis.exit.json').write_text(json.dumps({'returncode':1}))
d.report(out)
assert 'UNEVALUATED: analysis did not complete' in (out/'report.html').read_text()
assert 'points_1' not in json.loads((out/'summary.json').read_text())
(out/'points_1.analysis.exit.json').write_text(json.dumps({'returncode':0}))
print('PASS: existing analysis files without successful exit cannot receive a verdict')

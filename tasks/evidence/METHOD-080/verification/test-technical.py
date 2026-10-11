"""Final-path assertions only; numerical census outcomes are not read as results."""
import importlib.util, json, os, subprocess, sys, time
from pathlib import Path
SOURCE=Path(__file__).resolve().parents[1]/'census.py'
spec=importlib.util.spec_from_file_location('census',SOURCE)
c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
out=Path('/tmp/cpd-nystrom-census-20261011/technical');out.mkdir(exist_ok=True)
env=dict(os.environ,OPENBLAS_NUM_THREADS='1',OMP_NUM_THREADS='1');env.pop('INTRINSIC_NYSTROM_CAPTURE',None)
for cell in c.PLAN[:2]:
    name=cell[0];base=(name+'-baseline',cell[1],cell[2])
    for entry,binary in [(base,Path('/tmp/cpd-nystrom-row-20261011/bin/native')),(cell,c.BIN)]:
        current=dict(env)
        if entry==cell:current['INTRINSIC_NYSTROM_CAPTURE']=str(out/(name+'.snapshot'))
        with (out/(entry[0]+'.stdout')).open('w') as stdout,(out/(entry[0]+'.stderr')).open('w') as stderr:
            p=subprocess.run(['/usr/bin/time','-f','peak_rss_kib=%M','-o',str(out/(entry[0]+'.native.resources.txt'))]+c.native_command(entry,out,binary),env=current,stdout=stdout,stderr=stderr,preexec_fn=c.limits,timeout=90)
        assert p.returncode==0
    assert (out/(name+'.bin')).read_bytes()==(out/(base[0]+'.bin')).read_bytes()
    a=json.loads((out/(name+'.json')).read_text());b=json.loads((out/(base[0]+'.json')).read_text())
    for key in ['status','termination','iterations','nystrom_iterations','sigma2']:assert a[key]==b[key]
    assert [row[:8] for row in c.trace.traces(out/(name+'.stderr'))[0]]==[row[:8] for row in c.trace.traces(out/(base[0]+'.stderr'))[0]]
    c.dump(out/(name+'.native.exit.json'),{'returncode':0})
    start=time.monotonic()
    with (out/(name+'.analysis.stdout')).open('w') as stdout,(out/(name+'.analysis.stderr')).open('w') as stderr:
        p=subprocess.run(['/usr/bin/time','-f','peak_rss_kib=%M','-o',str(out/(name+'.analysis.resources.txt')),'taskset','-c','0-7',sys.executable,str(SOURCE),'analyze',str(out),name],env=env,stdout=stdout,stderr=stderr,preexec_fn=c.limits,timeout=180)
    assert p.returncode==0,(out/(name+'.analysis.stderr')).read_text()
    c.dump(out/(name+'.analysis.exit.json'),{'returncode':0})
    print('PASS:',name,'endpoint/trace parity, full census coverage, rejection preserved, exact first-row oracle, analysis budget',round(time.monotonic()-start,1),'seconds',flush=True)
    for phase in ['native','analysis']:
        evidence=(out/(name+'.'+phase+'.resources.txt')).read_text().strip()
        assert evidence.startswith('peak_rss_kib=') and int(evidence.split('=')[1])>0
        print('MEASURED:',name,phase,evidence,flush=True)
c.report(out)
assert (out/'report.html').exists() and len(list(out.glob('*-trace.png')))==2
c.dump(out/'failure.json',{'cell':'control','error':'known technical failure'});c.report(out)
assert 'UNEVALUATED: technical failure' in (out/'report.html').read_text();(out/'failure.json').unlink()
c.dump(out/'points_1.analysis.exit.json',{'returncode':1});c.report(out)
assert 'UNEVALUATED: analysis not completed' in (out/'report.html').read_text()
c.dump(out/'points_1.analysis.exit.json',{'returncode':0})
print('PASS: report path, failure and missing-exit gates; no efficacy outcomes inspected',flush=True)

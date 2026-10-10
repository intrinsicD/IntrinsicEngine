import hashlib,json,runpy,signal,tempfile,time
from pathlib import Path
ns=runpy.run_path('tasks/evidence/METHOD-078/diagnose.py',run_name='timeout_test')['run'].__globals__
with tempfile.TemporaryDirectory() as d:
 root=Path(d);(root/'reviews').mkdir(); frozen=b'test manifest'
 (root/'manifest.txt').write_bytes(frozen)
 (root/'reviews/approvals.json').write_text(json.dumps(dict(digest=hashlib.sha256(frozen).hexdigest(),claude='PASS',codex='PASS')))
 ns.update(HERE=root,PACKET=root,OUT=root/'output',ROOT=root)
 ns['plan']=lambda:[('timeout','test','test',2,.001)]
 ns['command']=lambda c,o:['sleep','10']
 ns['subprocess'].check_output=lambda *a,**kw:frozen
 real_alarm=signal.alarm
 ns['signal'].alarm=lambda seconds:real_alarm(1 if seconds else 0)
 reports=[]
 ns['report']=lambda:reports.append(json.loads((root/'output/failure.json').read_text()))
 start=time.monotonic()
 try: ns['run']()
 except TimeoutError: pass
 else: raise AssertionError('timeout not raised')
 assert time.monotonic()-start<3
 assert len(reports)==1 and 'TimeoutError' in reports[0]['error']
 print('PASS: actual process-wide alarm kills child, persists failure before recovery report, and raises TimeoutError')

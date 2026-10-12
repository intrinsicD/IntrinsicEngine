"""Remove each review repair in memory; the known-answer check must fail."""
import subprocess, sys
from pathlib import Path

here=Path(__file__).resolve().parents[1]
mutations={
 'native-frame-null':('min(identity,initialized)','identity'),
 'missing-auto':("r.get('auto_agreement')","r.get('auto_agreement',False)"),
 'finite-original':("assert all(math.isfinite(d[k]) for k in ('sigma2','scale','final_matched_mass'))","pass"),
 'replay-local':('[0,1,2,3,4,5,6,7,9]','[0,1,2,3,4,5,6,7]'),
 'replay-final':("and equal_trace and equal_final)","and equal_trace)"),
 'replay-censoring':("if p is not None and first and first[3] is not None:","if p is not None and first:"),
}
for name,(old,new) in mutations.items():
 script="""import runpy,sys,types
from pathlib import Path
here=Path(sys.argv[1]);sys.path.insert(0,str(here))
source=(here/'comparison.py').read_text();assert sys.argv[2] in source
module=types.ModuleType('comparison');module.__file__=str(here/'comparison.py')
sys.modules['comparison']=module
exec(compile(source.replace(sys.argv[2],sys.argv[3]),module.__file__,'exec'),module.__dict__)
runpy.run_path(str(here/'check.py'),run_name='__main__')
"""
 result=subprocess.run([sys.executable,'-c',script,str(here),old,new],capture_output=True,text=True,timeout=60)
 assert result.returncode!=0,(name,result.stdout)
 assert 'AssertionError' in result.stderr or 'ValueError: Out of range float values' in result.stderr,(name,result.stderr)
 print('PASS:',name,'removal rejected:',result.stderr.splitlines()[-1],flush=True)

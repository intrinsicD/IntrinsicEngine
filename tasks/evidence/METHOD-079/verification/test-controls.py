"""Known-answer diagnostic controls and mutations; no empirical result inspection."""
import os
from pathlib import Path
import resource
import subprocess
ROOT=Path(__file__).resolve().parents[4]
OUT=Path('/tmp/cpd-nystrom-row-20261011')
SOURCE=ROOT/'tasks/evidence/METHOD-079/analyze.cpp'
INCLUDE=ROOT/'external/vcpkg-installed/ci-release/x64-linux/include/eigen3'

def limits():
    resource.setrlimit(resource.RLIMIT_CORE,(0,0))
    resource.setrlimit(resource.RLIMIT_AS,(8<<30,8<<30))

def compile(source,target):
    r=subprocess.run(['/bin/clang++-23','-std=c++23','-O3','-I',str(INCLUDE),str(source),'-o',str(target)],capture_output=True,text=True,timeout=90)
    assert r.returncode==0,r.stderr

compile(SOURCE,OUT/'bin/analyze')
r=subprocess.run([str(OUT/'bin/analyze'),'--self-test'],capture_output=True,text=True,timeout=30,preexec_fn=limits)
assert r.returncode==0,r.stderr
print(r.stdout.strip())
text=SOURCE.read_text()
for name,old,new in [('no-truncation','values[i]>cutoff*top','values[i]>0'),('lose-sign','return !std::isfinite(v)','v=std::abs(v); return !std::isfinite(v)'),('miss-hard-zero','v>=-708','v>=-745')]:
    assert old in text
    source=OUT/'technical'/f'{name}.cpp';source.write_text(text.replace(old,new))
    target=OUT/'technical'/name;compile(source,target)
    r=subprocess.run([str(target),'--self-test'],capture_output=True,text=True,timeout=30,preexec_fn=limits)
    assert r.returncode!=0,name+' mutant escaped'
    print('PASS: self-test rejects',name)

"""Known-answer checks and mechanism-removal mutations; no run outcomes inspected."""
import importlib.util, math, os, struct, subprocess, sys, tempfile
from pathlib import Path
import numpy as np
from scipy.spatial import cKDTree
SOURCE=Path(__file__).resolve().parents[1]/'census.py'
spec=importlib.util.spec_from_file_location('census',SOURCE)
c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)

def exact_check(fn):
    answer=fn([0,0,0],np.array([[0,0,0],[1,0,0]]),np.log([1,2]),1)
    assert abs(float(answer)-math.log(1+2*math.exp(-.5)))<1e-14
    assert fn([1000,0,0],np.array([[0,0,0]]),np.array([0.]),.5)==-1000000

def mass_check(fn):
    assert abs(float(fn(math.log(3),1000,1000))-.75)<1e-14
    assert fn(-1000000,0,0)==0
    assert abs(float(fn(0,math.log(3),0))-.75)<1e-14

exact_check(c.exact);mass_check(c.mass)
r=math.exp(-.5);W=np.array([[1,r],[r,1]])
g=np.linalg.solve(W,np.array([r**4,r]));S=np.array([r,r**4])@g
assert abs(S-(2*r**5-r**3-r**9)/(1-r*r))<1e-14 and S<0 and math.exp(-4.5)>0
# Duplicates have a distinct neighbour at distance zero; the unique point excludes itself.
x=np.array([[0,0,0],[0,0,0],[3,0,0],[8,0,0]])
assert np.array_equal(cKDTree(x).query(x,k=2)[0][:,1],[0,0,3,5])
ids=np.arange(100000);pick=c.choose(ids,1024,8001)
assert len(pick)==len(np.unique(pick))==1024 and np.array_equal(pick,c.choose(ids,1024,8001))
assert np.array_equal(c.choose(ids[:4],1024,8001),ids[:4]) and len(np.unique(pick%20))==20
with tempfile.TemporaryDirectory() as temp:
    path=Path(temp)/'rows';values=np.ones(100000);values[5:9]=[-1,0,np.nan,-2]
    header=struct.pack('<4Q3d',80,100000,0,2,1,0,0)
    path.write_bytes(header+values.astype('<f8').tobytes()+bytes([1])*100000)
    got=c.rows(path)[4];assert got[5]==-1 and got[6]==0 and np.isnan(got[7]) and got[8]==-2
    def visited_check(fn):
        path.write_bytes(header+values.astype('<f8').tobytes()+bytes([1])*99999+b'\0')
        try:fn(path)
        except AssertionError:return
        raise AssertionError('missing row accepted')
    visited_check(c.rows)
    mutations=[('weights','np.asarray(lw,dtype=R)-np.sum','np.zeros_like(lw,dtype=R)-np.sum',exact_check,'exact'),('shift','v=logs+R(shift)-R(logc)','v=logs-R(logc)',mass_check,'mass'),('visits',"assert np.all(visited==1),'unvisited or multiply visited rows'",'pass',visited_check,'rows')]
    for name,old,new,check,func in mutations:
        text=SOURCE.read_text();assert old in text
        ns={'__file__':str(SOURCE),'__name__':'mutation'};exec(compile(text.replace(old,new),str(SOURCE),'exec'),ns)
        try:check(ns[func])
        except AssertionError:print('PASS: control rejects',name,'mutation')
        else:raise AssertionError(name+' escaped')
    # Import only: removing the guard cannot accidentally launch a scientific run.
    cmd=[sys.executable,'-O','-c','import runpy; runpy.run_path('+repr(str(SOURCE))+', run_name="import_only")']
    p=subprocess.run(cmd,capture_output=True,text=True,timeout=20)
    assert p.returncode!=0 and 'Optimized Python is forbidden' in p.stderr
print('PASS: exact log sums, shift/outlier mass, signed Gaussian approximation, self exclusion, row categories, seeded samples and optimization guard')

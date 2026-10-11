"""Native Solver::Current failure shape: zero Result.Iterations, nonzero observer history."""
import json,sys,tempfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]));import convergence as c
with tempfile.TemporaryDirectory() as tmp:
 folder=Path(tmp);cell=c.cells()[1];name=cell["name"]
 d=dict(fixture=cell["fixture"],seed=cell["seed"],points=c.N,targets=c.N,threads=8,variant=c.VARIANTS[cell["variant"]],landmarks=256,error_limit=.001,status="numerical_failure",termination="none",iterations=0,observed_iterations=2,nystrom_iterations=2,seconds=1.,rms_to_truth=None)
 (folder/(name+".json")).write_text(json.dumps(d))
 (folder/(name+".stderr")).write_text("CPD_STAGE expectation_step 0\nCPD_ITER 0 .1 20 20 100 5 .0001 1000 .2\nCPD_ITER 1 .09 19 19 100 5 .0001 1000 .4\n")
 row,trace,stages,positions=c.load_cell(cell,folder)
 assert row["status"]=="numerical_failure" and len(trace)==2 and positions is None
 print("PASS: native numerical failure retains completed observer history without endpoints")

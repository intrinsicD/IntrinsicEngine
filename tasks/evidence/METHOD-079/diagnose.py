#!/usr/bin/env python3
"""Frozen first-invalid-target diagnostic; no algorithm selection or speed claim."""
if not __debug__:
    raise RuntimeError("Optimized Python is forbidden: execution gates require assertions")
import hashlib
import html
import importlib.util
import json
import math
import os
from pathlib import Path
import resource
import signal
import subprocess
import time
import numpy as np
ROOT=Path(__file__).resolve().parents[3]
HERE=Path(__file__).resolve().parent
OUT=HERE/'runs'
PACKET=HERE/'preflight/pilot'
BINS=Path('/tmp/cpd-nystrom-row-20261011/bin')
PLAN=[('points_1','points/fixed/random',4),('box_1','box/resampled/random',2),('box_2','box/resampled/random',2),('points_2','points/fixed/random',4)]
MANIFEST=['/usr/bin/python3','/home/alex/.codex/skills/experiment-preflight/manifest.py','--exclude','tasks/evidence/METHOD-079/preflight/pilot','--exclude','tasks/evidence/METHOD-079/reviews','--exclude','tasks/evidence/METHOD-079/runs',str(BINS),'tasks/evidence/METHOD-079/preflight/pilot/layer-a.md','tasks/evidence/METHOD-079/preflight/pilot/layer-b.md']
spec=importlib.util.spec_from_file_location('method078_trace',HERE.parent/'METHOD-078/diagnose.py')
previous=importlib.util.module_from_spec(spec);spec.loader.exec_module(previous)

def native_command(cell,folder):
    name,variant,iterations=cell
    return ['taskset','-c','0-7',str(BINS/'native'),'--fixture','full','--seed','1','--points','100000','--threads','8','--iterations',str(iterations),'--trace','1','--variant',variant,'--landmarks','256','--error-limit','0.001','--out',str(folder/(name+'.json')),'--positions',str(folder/(name+'.bin'))]

def analysis_command(cell,folder):
    return ['taskset','-c','0-7',str(BINS/'analyze'),str(folder/(cell[0]+'.snapshot')),str(folder/cell[0])]

def limits():
    resource.setrlimit(resource.RLIMIT_AS,(8<<30,8<<30))
    resource.setrlimit(resource.RLIMIT_CORE,(0,0))

def metrics(path):
    result={}
    for line in path.read_text().splitlines():
        key,value=line.split('\t');assert key not in result
        try:
            number=float(value); result[key]=number if math.isfinite(number) else value
        except ValueError: result[key]=value
    return result

def verdict(m):
    keys=['native_sum','native_g_extended_dot','native_factors_extended_sum','native_gram_extended_eigen_sum','rebuilt_matched_rank_sum','rebuilt_sum']
    values=[m.get(k) for k in keys]
    if not all(isinstance(v,(int,float)) and math.isfinite(v) for v in values):
        return 'UNRESOLVED: nonfinite or missing evaluation; inspect captured operands'
    if all(v<0 for v in values):
        spread=max(values)-min(values)
        if spread>.01*abs(values[0]):
            return 'Negative sign persists, but magnitude is precision/operand sensitive (>1% spread)'
        if abs(m['rebuilt_sum'])<=m['rebuilt_accumulation_scale'] or abs(m['rebuilt_matched_rank_sum'])<=m['matched_accumulation_scale']:
            return 'UNRESOLVED: negative evaluations below accumulation-scale heuristic'
        return 'Negative sum persists across arithmetic/operand checks (spread <=1%, above accumulation-scale heuristic); no exact-arithmetic proof'
    if values[0]==0:
        return 'Native zero: inspect kernel zeros, stable exact sum and coverage; no automatic underflow attribution'
    return 'UNRESOLVED: evaluation paths disagree in sign or original failure was not reproduced'

def report(folder=OUT):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    body=['<h1>METHOD-079: first invalid Nyström target sum</h1>',
          '<p>CPU diagnostic, full synthetic fixture, seed 1; 4 points/fixed or 2 box/resampled iterations. '
          'No production change, performance claim or complete-registration verdict. Long double is a sensitivity check. '
          'Selected row: lowest invalid target index in the first failing attempt. Internal normalized coordinates and sigma are kept separate from world-space iteration traces.</p>']
    completed=[]
    for planned in PLAN:
        name=planned[0];native_status=folder/(name+'.native.exit.json');analysis_status=folder/(name+'.analysis.exit.json')
        if native_status.exists() and analysis_status.exists():
            ns=json.loads(native_status.read_text());an=json.loads(analysis_status.read_text())
            if ns.get('returncode')==0 and (an.get('returncode')==0 or an.get('reason')=='no snapshot'):completed.append(name)
    pending=[c[0] for c in PLAN if c[0] not in completed]
    body += ['<p>Recorded completed cells: '+html.escape(', '.join(completed))+'; pending: '+html.escape(', '.join(pending))+'.</p>',
             '<p>'+('Plan completion recorded.' if (folder/'completion.json').exists() else 'INCOMPLETE: plan completion not recorded.')+'</p>']
    allmetrics={}
    failure=folder/'failure.json'
    if failure.exists():body+=['<h2>UNEVALUATED: technical failure</h2><pre>'+html.escape(failure.read_text())+'</pre>']
    (folder/'report.html').write_text('<!doctype html><meta charset="utf-8">'+''.join(body)+'<p>Report assembly incomplete; raw outputs retained, no verdict.</p>')
    preview={k:[] for k in ['source','target','truth']}
    import re
    for line in (HERE.parent/'METHOD-078/fixture-preview.txt').read_text().splitlines():
        match=re.match(r'(source|target|truth)\[(\d+)\] (.*)',line)
        if match:preview[match[1]].append((int(match[2]),tuple(map(float,match[3].split()))))
    for cell in PLAN:
        name=cell[0];native=folder/(name+'.json'); log=folder/(name+'.stderr'); data=folder/(name+'.analysis.tsv')
        if not native.exists() and not log.exists():continue
        body+=[f'<h2>{name}</h2>']
        exit_file=folder/(name+'.analysis.exit.json')
        analysis_ok=exit_file.exists() and json.loads(exit_file.read_text()).get('returncode')==0
        if analysis_ok and data.exists() and all((folder/(name+ext)).exists() for ext in ['.spectrum.csv','.sources.bin','.top.csv']):
            m=metrics(data);allmetrics[name]=m
            m['target_is_injected_clutter']=int(m['target_index'])%20==0
            body+=['<p><b>'+html.escape('UNEVALUATED: run failure; raw values only' if failure.exists() else verdict(m))+'</b></p>', '<pre>'+html.escape(json.dumps(m,indent=2,allow_nan=False))+'</pre>']
            spectrum=np.genfromtxt(folder/(name+'.spectrum.csv'),delimiter=',',names=True)
            source=np.fromfile(folder/(name+'.sources.bin'),dtype='<f8').reshape(-1,3)
            fig,axes=plt.subplots(1,3,figsize=(15,4))
            for key in ['native','native_gram_extended','rebuilt']:axes[0].plot(spectrum['index'],np.abs(spectrum[key]),label=key)
            axes[0].axhline(1e-10*m['native_top'],color='red',linestyle=':');axes[0].set(yscale='log',title='Absolute eigenvalues; red = native cutoff');axes[0].legend()
            stride=max(1,len(source)//4000);subset=source[::stride]
            axes[1].scatter(subset[:,0],subset[:,1],s=2);axes[1].set_yscale('symlog',linthresh=1e-15);axes[1].set(xlabel='Exact shifted log affinity',ylabel='Rebuilt signed affinity',title='Source contributions (uniform index sample)')
            weights=np.exp(source[:,0]-source[:,0].max());weights/=weights.sum()
            axes[2].hist(source[:,2],bins=40,weights=weights);axes[2].set(xlabel='Source-to-nearest-landmark distance / sigma',ylabel='Exact row-mass share',title='Coverage of exact contributing sources')
            fig.tight_layout();fig.savefig(folder/(name+'-diagnostic.png'));plt.close(fig)
            body+=[f'<img width="1200" src="{name}-diagnostic.png" alt="Spectrum, signed source affinities and exact-mass-weighted coverage">', '<pre>'+html.escape((folder/(name+'.top.csv')).read_text())+'</pre>']
        else:body+=['<p>UNEVALUATED: analysis did not complete or no snapshot; row mechanism unresolved.</p>']
        if log.exists():
            spectrum_rows=[list(map(float,line.split()[1:])) for line in log.read_text().splitlines() if line.startswith('NYSTROM_SPECTRUM ')]
            if spectrum_rows:
                fig,axes=plt.subplots(1,3,figsize=(13,3));indices=range(len(spectrum_rows))
                for ax,ys,label in [(axes[0],[v[2] for v in spectrum_rows],'Internal sigma²'),(axes[1],[v[4]/v[5] for v in spectrum_rows],'Retained spectral condition'),(axes[2],[v[8] for v in spectrum_rows],'Strongest-pair distance / sigma in smallest kept mode')]:
                    ax.plot(indices,ys,'o-');ax.set_title(label);ax.set_xlabel('Attempt index');ax.set_yscale('log')
                fig.tight_layout();fig.savefig(folder/(name+'-attempts.png'));plt.close(fig)
                body += [f'<img width="1100" src="{name}-attempts.png" alt="Spectrum diagnostics over all attempts">', '<pre>draw landmarks sigma² rank top smallest-kept pair-a pair-b pair-distance/sigma\n'+html.escape('\n'.join(line for line in log.read_text().splitlines() if line.startswith('NYSTROM_SPECTRUM ')))+'</pre>']
            values,attempts,begins,costs,stages=previous.traces(log)
            fig,axes=plt.subplots(2,3,figsize=(13,7));x=[v[0] for v in values]
            for ax,col,label in [(axes[0,0],2,'NLL: raw = weighted (weight 1)'),(axes[0,1],3,'Stopping objective'),(axes[0,2],1,'Post-M-step sigma², world units²'),(axes[1,0],5,'Actual E-step policy'),(axes[1,1],6,'Accepted sampled error')]:
                ax.plot(x,[v[col] for v in values]);ax.set_title(label);ax.set_xlabel('Iteration')
                if name in allmetrics:ax.axvline(allmetrics[name]['draw'],color='red',linestyle=':')
            axes[0,1].plot(x,[v[3]-v[2] for v in values],label='objective minus NLL (BCPD prior: 0)');axes[0,1].legend()
            labels=list(dict.fromkeys(s for s,t in stages));axes[1,2].step([t for s,t in stages],[labels.index(s) for s,t in stages],where='post');axes[1,2].set_yticks(range(len(labels)),labels);axes[1,2].set_title('Stage boundaries (seconds; not a timing comparison)')
            fig.tight_layout();fig.savefig(folder/(name+'-trace.png'));plt.close(fig)
            body+=[f'<img width="1100" src="{name}-trace.png" alt="Every observed iteration and stage boundary">', '<p>NLL has weight 1 and share 1 in the implemented Bayesian stopping objective; extra prior contribution is zero. No full variational ELBO is claimed.</p>', '<pre>'+html.escape('\n'.join(s for s in log.read_text().splitlines() if s.startswith('NYSTROM_DIAG')))+'</pre>']
        positions=folder/(name+'.bin')
        if positions.exists() and positions.stat().st_size==2400000:
            xyz=np.fromfile(positions,dtype='<f8').reshape(-1,3);indices=[i for i,p in preview['source']];before=np.array([p for i,p in preview['source']]);after=xyz[indices]
            target=np.array([p for i,p in preview['target']]);truth=np.array([p for i,p in preview['truth']]);points=np.concatenate([before,after,target,truth]);lo=points.min(0);hi=points.max(0)
            fig=plt.figure(figsize=(10,4))
            for k,(cloud,title) in enumerate([(before,'Before'),(after,'After bounded prefix')],1):
                ax=fig.add_subplot(1,2,k,projection='3d')
                for p,label in [(target,'target'),(truth,'truth'),(cloud,'source')]:ax.scatter(*p.T,s=2,label=label)
                ax.set(xlim=(lo[0],hi[0]),ylim=(lo[1],hi[1]),zlim=(lo[2],hi[2]),title=title);ax.legend()
            fig.tight_layout();fig.savefig(folder/(name+'-positions.png'));plt.close(fig)
            body+=[f'<img width="1000" src="{name}-positions.png" alt="Before and after, same axes and source subset">']
        body+=[f'<p><a href="{name}.snapshot">Raw operands</a> · <a href="{name}.analysis.tsv">Raw analysis</a> · <a href="{name}.components.csv">Eigencomponent terms</a> · <a href="{name}.stderr">Native trace</a></p>']
    for stem in ['points','box']:
        a,b=stem+'_1',stem+'_2'
        if a in allmetrics and b in allmetrics:
            equal=allmetrics[a]==allmetrics[b] and (folder/(a+'.snapshot')).read_bytes()==(folder/(b+'.snapshot')).read_bytes()
            body+=[f'<p>{stem}: repeated snapshots and metrics identical = {equal}</p>']
            if not equal:body+=['<p>UNRESOLVED: deterministic replay mismatch.</p>']
    (folder/'summary.json').write_text(json.dumps(allmetrics,indent=2,allow_nan=False))
    (folder/'report.html').write_text('<!doctype html><meta charset="utf-8"><style>body{font-family:system-ui;max-width:1250px;margin:auto}pre{white-space:pre-wrap}img{max-width:100%}</style>'+''.join(body))

def run():
    frozen=(PACKET/'manifest.txt').read_bytes();digest=hashlib.sha256(frozen).hexdigest()
    approved=json.loads((HERE/'reviews/approvals.json').read_text())
    assert approved['digest']==digest and approved['claude']==approved['codex']=='PASS'
    OUT.mkdir(exist_ok=False)
    def expired(signum,frame):raise TimeoutError('total 900-second budget exhausted or terminated')
    signal.signal(signal.SIGALRM,expired);signal.signal(signal.SIGTERM,expired);signal.alarm(900)
    start=time.monotonic();name='before-first-cell'
    try:
        for cell in PLAN:
            name=cell[0]
            env=dict(os.environ,INTRINSIC_NYSTROM_CAPTURE=str(OUT/(name+'.snapshot')))
            for phase,args,seconds in [('native',native_command(cell,OUT),90),('analysis',analysis_command(cell,OUT),120)]:
                assert subprocess.check_output(MANIFEST,cwd=ROOT)==frozen,'manifest drift'
                remaining=900-(time.monotonic()-start)
                if remaining<=0:raise TimeoutError('total budget exhausted')
                if phase=='analysis' and not (OUT/(name+'.snapshot')).exists():
                    (OUT/(name+'.analysis.exit.json')).write_text(json.dumps({'returncode':None,'reason':'no snapshot'}));break
                (OUT/(name+'.'+phase+'.command.json')).write_text(json.dumps({'argv':args,'capture_env':env['INTRINSIC_NYSTROM_CAPTURE']}))
                suffix='.stdout' if phase=='native' else '.analysis.tsv'
                with (OUT/(name+suffix)).open('w') as out,(OUT/(name+('.stderr' if phase=='native' else '.analysis.stderr'))).open('w') as err:
                    r=subprocess.run(args,cwd=ROOT,env=env,stdout=out,stderr=err,timeout=min(seconds,remaining),preexec_fn=limits)
                (OUT/(name+'.'+phase+'.exit.json')).write_text(json.dumps({'returncode':r.returncode}))
                if r.returncode:raise RuntimeError(f'{name} {phase}: exit {r.returncode}')
                if phase=='native':
                    v=previous.traces(OUT/(name+'.stderr'))[0];j=json.loads((OUT/(name+'.json')).read_text())
                    assert len(v)==j['iterations']==j['observed_iterations'] and 0<=j['iterations']<=cell[2]
                    assert (OUT/(name+'.bin')).stat().st_size==2400000
                (OUT/(name+'.resources.json')).write_text(json.dumps({'max_completed_child_rss_kib_so_far':resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss}))
            report();print('Completed',name,flush=True)
        (OUT/'completion.json').write_text(json.dumps({'cells':[c[0] for c in PLAN]}));report()
    except BaseException as error:
        signal.alarm(0);(OUT/'failure.json').write_text(json.dumps({'cell':name,'error':f'{type(error).__name__}: {error}'}))
        report();raise
    finally:signal.alarm(0)

if __name__=='__main__':
    import sys
    if sys.argv[1:] == ['run']:run()
    elif sys.argv[1:] == ['report']:report()
    elif sys.argv[1:] == ['plan']:print(json.dumps([native_command(c,OUT) for c in PLAN],indent=2))
    else:raise SystemExit('usage: diagnose.py run|report|plan')

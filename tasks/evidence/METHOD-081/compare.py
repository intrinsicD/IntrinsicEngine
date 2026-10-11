#!/usr/bin/env python3
"""Frozen, local METHOD-081 diagnostic; not a general performance benchmark."""
import base64, hashlib, html, io, json, math, os, re, resource, signal, subprocess, sys, time
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[3]
HERE=Path(__file__).resolve().parent
OUT=HERE/'runs'; PACKET=HERE/'preflight'
BINS=Path('/tmp/cpd-nystrom-repair-20261011/bin')

def cells():
    pairs=[(a+'_'+v,a,v,k) for a in ('baseline','candidate') for v,k in [('points',7),('box',2)]]
    return [('exact_7','baseline','exact',7),('exact_2','baseline','exact',2)]+[(n+'_1',a,v,k) for n,a,v,k in pairs]+[(n+'_2',a,v,k) for n,a,v,k in reversed(pairs)]

def command(cell, folder):
    name,arm,variant,iters=cell
    variant={'points':'points/fixed/random','box':'box/resampled/random','exact':'exact'}[variant]
    args=['taskset','-c','0-7',str(BINS/arm),'--fixture','full','--seed','1','--points','100000','--threads','8','--iterations',str(iters),'--trace','1','--variant',variant,'--landmarks','256','--error-limit','0.001','--out',str(folder/(name+'.json')),'--positions',str(folder/(name+'.bin'))]
    if cell[2]!='exact': args+=['--reference',str(folder/('exact_'+str(iters)+'.bin'))]
    return args

def manifest_command():
    return ['/usr/bin/python3','/home/alex/.codex/skills/experiment-preflight/manifest.py',
            '--exclude','tasks/evidence/METHOD-081/preflight','--exclude','tasks/evidence/METHOD-081/reviews',
            '--exclude','tasks/evidence/METHOD-081/runs','--exclude','tasks/evidence/METHOD-081/verification',
            str(BINS),'tasks/evidence/METHOD-081/preflight/layer-a.md','tasks/evidence/METHOD-081/preflight/layer-b.md',
            'tasks/evidence/METHOD-081/verification/mutations.py','tasks/evidence/METHOD-081/verification/mutations.log',
            'tasks/evidence/METHOD-081/verification/focused-tests.log',
            'tasks/evidence/METHOD-081/verification/report-check.py','tasks/evidence/METHOD-081/verification/report-check.log']

def positions(path):
    data=np.fromfile(path,dtype='<f8');assert data.size in (300000,400000),'position shape'
    value=data.reshape(100000,-1)[:,:3];assert np.isfinite(value).all(),'nonfinite positions'
    return value

def distances(a,b):
    d=np.linalg.norm(a-b,axis=1)
    return float(np.sqrt(np.mean(d*d))),float(np.max(d))

def quality(rms,maximum): return math.isfinite(rms+maximum) and rms<=0.001 and maximum<=0.01

def improved(delta): return min(delta.values())>=0 and max(delta.values())>0

def traces(path):
    rows=[];stages=[]
    for line in path.read_text().splitlines():
        p=line.split()
        if p[:1]==['CPD_ITER']:
            assert len(p)==10
            row=list(map(float,p[1:]));assert all(math.isfinite(v) for v in row);rows.append(row)
        if p[:1]==['CPD_STAGE']: stages.append((p[1],float(p[2])))
    return rows,stages

def read(cell,folder):
    name,arm,variant,k=cell
    d=json.loads((folder/(name+'.json')).read_text());p=positions(folder/(name+'.bin'))
    t,stages=traces(folder/(name+'.stderr'))
    assert d['status']=='success' and d['iterations']==k and len(t)==k
    assert [int(r[0]) for r in t]==list(range(k)) or [int(r[0]) for r in t]==list(range(1,k+1))
    assert sum(r[5]==5 for r in t)==d['nystrom_iterations']
    assert math.isfinite(d['seconds']) and d['seconds']>0 and stages
    if variant!='exact':
        rms,mx=distances(p,positions(folder/('exact_'+str(k)+'.bin')))
        assert math.isclose(rms,d['rms_to_exact'],rel_tol=1e-10,abs_tol=1e-13)
        assert math.isclose(mx,d['max_to_exact'],rel_tol=1e-10,abs_tol=1e-13)
        d['quality_pass']=quality(rms,mx)
        exact=json.loads((folder/('exact_'+str(k)+'.json')).read_text())
        d['outcome_match']=all(d[key]==exact[key] for key in ('status','termination','iterations'))
    else: assert d['nystrom_iterations']==0
    return d,t,stages,p

def figure_url(fig):
    stream=io.BytesIO();fig.savefig(stream,format='png',dpi=110)
    return 'data:image/png;base64,'+base64.b64encode(stream.getvalue()).decode()

def report():
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    entries={};invalid={};body=['<!doctype html><meta charset="utf-8"><title>METHOD-081</title><style>body{font:16px system-ui;max-width:1200px;margin:30px auto}img{max-width:100%}td,th{padding:6px;border-bottom:1px solid #ddd}pre{white-space:pre-wrap}</style><h1>Exact replacement of invalid Nyström rows</h1>',
        '<p>Local diagnostic: one synthetic fixture, seed 1; fixed FPS (7 iterations), resampled union-box (2). Two reversed-order repetitions measure deterministic replay and a timing range, not population uncertainty. Endpoints are compared to the matching Auto prefix. No full-convergence or general speed claim.</p>',
        '<p>Frozen endpoint gates: RMS ≤ 0.001 world units; maximum ≤ 0.01; outcome unchanged. Mechanism prediction: at least one additional accepted Nyström iteration, without decreasing either variant. Positive sums do not establish positive pair affinities.</p>',
        '<p>The trace exposes NLL and the stopping objective, not a full variational ELBO decomposition. NLL weight is 1 (raw = weighted, share 100% of the logged NLL). Objective-minus-NLL is shown as a diagnostic difference, not an inferred loss term.</p>']
    preview={'source':[],'target':[],'truth':[]}
    for line in (HERE/'fixture-preview.txt').read_text().splitlines():
        match=re.match(r'(source|target|truth)\[(\d+)\] (.*)',line)
        if match: preview[match[1]].append((int(match[2]),list(map(float,match[3].split()))))
    for cell in cells():
        name=cell[0]
        if not (OUT/(name+'.json')).exists():continue
        try:d,t,stages,p=read(cell,OUT)
        except (AssertionError,ValueError,OSError,KeyError) as error:
            if not (OUT/'failure.json').exists():raise
            invalid[name]=type(error).__name__+': '+str(error);continue
        entries[name]=d
        body+=['<h2>'+name+'</h2>','<pre>'+html.escape(json.dumps(d,indent=2))+'</pre>']
        t=np.asarray(t);fig,axes=plt.subplots(4,2,figsize=(12,12))
        for ax,col,label in [(axes[0,0],2,'NLL, raw = weighted (weight 1)'),(axes[0,1],3,'Stopping objective'),(axes[1,0],1,'Sigma² (world units²)'),(axes[1,1],6,'Accepted sampled relative error'),(axes[2,0],5,'E-step policy (5 = Nyström)'),(axes[3,0],4,'Matched mass'),(axes[3,1],7,'Kernel evaluations (count)')]:
            ax.plot(t[:,0],t[:,col]);ax.set(title=label,xlabel='Iteration')
            for j in range(1,len(t)):
                if t[j,5]!=t[j-1,5]:ax.axvline(t[j,0],color='red',linestyle=':')
        axes[0,1].plot(t[:,0],t[:,3]-t[:,2],label='Objective minus NLL');axes[0,1].legend()
        labels=list(dict.fromkeys(s for s,_ in stages));axes[2,1].step([s for _,s in stages],[labels.index(x) for x,_ in stages],where='post');axes[2,1].set_yticks(range(len(labels)),labels);axes[2,1].set(title='Recorded stages',xlabel='Elapsed seconds')
        fig.tight_layout();body+=['<img alt="Iteration diagnostics" src="'+figure_url(fig)+'">'];plt.close(fig)
        before=np.asarray([q for _,q in preview['source']]);after=p[[i for i,_ in preview['source']]]
        target=np.asarray([q for _,q in preview['target']]);truth=np.asarray([q for _,q in preview['truth']]);allp=np.vstack([before,after,target,truth]);lo=allp.min(axis=0);hi=allp.max(axis=0)
        fig=plt.figure(figsize=(12,5))
        for j,(cloud,title) in enumerate([(before,'Before'),(after,'After')],1):
            ax=fig.add_subplot(1,2,j,projection='3d')
            for x,label in [(target,'target'),(truth,'truth'),(cloud,'source')]:ax.scatter(*x.T,s=3,label=label)
            ax.set(title=title,xlabel='x (world units)',ylabel='y (world units)',zlabel='z (world units)',xlim=(lo[0],hi[0]),ylim=(lo[1],hi[1]),zlim=(lo[2],hi[2]));ax.legend()
        fig.tight_layout();body+=['<p>Display subset: first and last 100 points; not used for endpoint metrics.</p><img alt="Before and after" src="'+figure_url(fig)+'">'];plt.close(fig)
    summary={'complete':len(entries)==len(cells()),'rows':entries,'invalid_cells':invalid}
    if (OUT/'failure.json').exists():summary['failure']=json.loads((OUT/'failure.json').read_text());summary['complete']=False
    if summary['complete']:
        summary['quality_pass']=all(d.get('quality_pass',True) and d.get('outcome_match',True) for d in entries.values())
        summary['repeat_identity']=all((OUT/(a+'_'+v+'_1.bin')).read_bytes()==(OUT/(a+'_'+v+'_2.bin')).read_bytes() and [r[:8] for r in traces(OUT/(a+'_'+v+'_1.stderr'))[0]]==[r[:8] for r in traces(OUT/(a+'_'+v+'_2.stderr'))[0]] for a in ('baseline','candidate') for v in ('points','box'))
        summary['accepted_delta']={v:entries['candidate_'+v+'_1']['nystrom_iterations']-entries['baseline_'+v+'_1']['nystrom_iterations'] for v in ('points','box')}
        delta=summary['accepted_delta'];summary['mechanism_prediction']=improved(delta)
        summary['verdict']='BOUNDED PREDICTION SUPPORTED' if summary['quality_pass'] and summary['repeat_identity'] and summary['mechanism_prediction'] else 'BOUNDED PREDICTION NOT SUPPORTED'
    else:summary['verdict']='INCOMPLETE / UNEVALUATED'
    (OUT/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    (OUT/'report.html').write_text(''.join(body[:4]+['<pre>'+html.escape(json.dumps({k:v for k,v in summary.items() if k!='rows'},indent=2))+'</pre>']+body[4:]))
    return summary

def limits():resource.setrlimit(resource.RLIMIT_AS,(3*1024**3,)*2)

def self_test():
    a=np.array([[1.,0,0],[2.,0,0],[3.,0,0]])
    assert distances(a,a)==(0.,0.) and quality(*distances(a,a))
    for bad in [np.zeros_like(a),np.repeat(a.mean(axis=0)[None,:],3,axis=0),a+0.02]:assert not quality(*distances(a,bad))
    assert not quality(float('nan'),0.) and not quality(0.,float('inf'))
    assert not improved({'points':0,'box':0}) and not improved({'points':1,'box':-1}) and improved({'points':1,'box':0})
    assert len(cells())==10 and len({x[0] for x in cells()})==10
    assert all(command(c,OUT)[0:3]==['taskset','-c','0-7'] for c in cells())
    print('PASS: identity positive control; zero/collapse/displacement/nonfinite negative controls; fixed cells and pinning')

def run():
    approval=json.loads((HERE/'reviews/approval.json').read_text());expected=hashlib.sha256((PACKET/'manifest.txt').read_bytes()).hexdigest()
    assert approval['digest']==expected and approval['claude']=='PASS' and approval['codex']=='PASS'
    OUT.mkdir(exist_ok=False);start=time.monotonic()
    def expired(signum,frame): raise TimeoutError('total driver budget')
    signal.signal(signal.SIGALRM,expired);signal.alarm(1200)
    try:
        for cell in cells():
            assert hashlib.sha256(subprocess.check_output(manifest_command(),cwd=ROOT)).hexdigest()==expected,'frozen inputs changed'
            remaining=1200-(time.monotonic()-start);assert remaining>0,'total time budget'
            cmd=command(cell,OUT);name=cell[0]
            (OUT/(name+'.command.json')).write_text(json.dumps(cmd,indent=2)+'\n')
            print('START',name,flush=True)
            with (OUT/(name+'.stdout')).open('w') as stdout,(OUT/(name+'.stderr')).open('w') as stderr:
                subprocess.run(cmd,cwd=ROOT,stdout=stdout,stderr=stderr,check=True,timeout=min(240,remaining),preexec_fn=limits)
            read(cell,OUT)
            (OUT/(name+'.resources.json')).write_text(json.dumps({'max_completed_child_rss_kib_so_far':resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss})+'\n')
            report();print('DONE',name,flush=True)
    except Exception as error:
        signal.alarm(0)
        (OUT/'failure.json').write_text(json.dumps({'error':str(error)})+'\n');report();raise
    signal.alarm(0)
    print(json.dumps({k:v for k,v in report().items() if k!='rows'}),flush=True)

if __name__=='__main__':
    if not __debug__:raise RuntimeError('Assertions are required')
    {'self-test':self_test,'run':run,'report':report}[sys.argv[1]]()

"""METHOD-083 bounded CPU diagnostic; native stops and shared endpoint quality are distinct."""
import asyncio, hashlib, html, json, math, os, re, signal, subprocess, sys, time
from pathlib import Path
import numpy as np
HERE=Path(__file__).resolve().parent; ROOT=HERE.parents[2]
sys.path.insert(0,str(HERE.parent/'METHOD-081'))
from compare import distances, quality
BIN=Path('/tmp/method083/bin'); INPUT=Path('/tmp/method083/inputs'); OUT=HERE/'runs'
N,CAP,CELL_SECONDS,WORKERS=100000,5000,1800,2
ARMS=('baseline_auto','baseline_points','baseline_box','adapted_auto','adapted_points','adapted_box','adapted_union','upstream_nystrom','upstream_direct')
VARIANTS=dict(auto='exact',points='points/fixed/random',box='box/resampled/random',union='union/fixed/random')
TOTAL_SECONDS=math.ceil((6*len(ARMS)+4)/WORKERS)*CELL_SECONDS+1800

def cells():
    rows=[dict(name=f'{f}_{s}_{a}',fixture=f,seed=s,arm=a) for f in ('full','partial') for s in (1,2,3) for a in ARMS]
    return rows+[dict(c,name=c['name']+'_repeat') for c in rows if c['seed']==1 and c['arm'] in ('adapted_union','upstream_nystrom')]

def command(c,folder,slot=0):
    f=f"{c['fixture']}_{c['seed']}"; name=c['name']; arm,variant=c['arm'].split('_',1)
    prefix=['taskset','-c','0-7' if slot==0 else '8-15','/usr/bin/time','-f','%M','-o',str(folder/(name+'.rss')),'timeout','--kill-after=5s',str(CELL_SECONDS)+'s','prlimit','--as='+str(8*1024**3),'--',str(BIN/arm)]
    if arm=='upstream':
        return prefix+['-x',str(INPUT/(f+'_target.txt')),'-y',str(INPUT/(f+'_source.txt')),'-o',str(folder/(name+'_')),'-w','0.1','-b','1','-l','2','-g','1','-K','100','-J','256' if variant=='nystrom' else '0','-n',str(CAP),'-N','30','-c','0.00001','-r',str(c['seed']),'-u','n','-T','srn','-p','-f','0.2','-d','7','-e','0.15','-h','-s','ytT']
    return prefix+['--fixture',c['fixture'],'--seed',str(c['seed']),'--points',str(N),'--threads','8','--iterations',str(CAP),'--trace','1','--variant',VARIANTS[variant],'--landmarks','256','--error-limit','0.001','--switch','0.2','--rank','100','--beta','1','--tolerance','0.00001','--source',str(INPUT/(f+'_source.txt')),'--target',str(INPUT/(f+'_target.txt')),'--truth',str(INPUT/(f+'_truth.txt')),'--out',str(folder/(name+'.json')),'--positions',str(folder/(name+'.bin'))]

def manifest_command():
    extras=[str(BIN),str(INPUT),str(HERE/'preflight/layer-a.md'),str(HERE/'preflight/layer-b.md')]
    extras += [str(HERE/'verification'/p) for p in ('adapter-check.py','adapter-check.log','input-check.log','technical.py','technical.log','check.log','review-controls-before.log','review-mutations.py','review-mutations.log','replay-before.log','terminal.py','terminal.log','terminal-raw/sha256.json','baseline-binding.json','adapted-binding.json')]
    extras += json.loads((HERE/'environment.json').read_text())['runtime_library_paths']
    return ['/usr/bin/python3','/home/alex/.codex/skills/experiment-preflight/manifest.py',*sum((['--exclude',str(HERE/p)] for p in ('preflight','reviews','runs','verification')),[]),*extras]

def traces(path):
    # METHOD-081's frozen parser requires nine fields; this adapter adds the local-transition flag.
    rows=[];stages=[]
    for line in path.read_text().splitlines():
        fields=line.split()
        if fields[:1]==['CPD_ITER']:
            assert len(fields)==11
            row=list(map(float,fields[1:]));assert all(math.isfinite(v) for v in row)
            assert row[9] in (0,1);rows.append(row)
        elif fields[:1]==['CPD_STAGE']:
            assert len(fields)==3 and math.isfinite(float(fields[2]));stages.append((fields[1],float(fields[2])))
    return rows,stages

def load(c,folder,outcome):
    name=c['name']; upstream=c['arm'].startswith('upstream_'); p=None
    if upstream:
        stderr=folder/(name+'.stderr')
        text=stderr.read_text() if stderr.exists() else ''
        assert stderr.exists() or outcome['kind']=='unevaluated'
        pattern=r'loop=(\d+)\s+acc.P=\[([^]]+)\]\s+N_hat=([-\d.e+]+)\s+sigma=([-\d.e+]+)'
        t=[]
        for k,policy,mass,sigma in re.findall(pattern,text):
            assert math.isfinite(float(mass)) and math.isfinite(float(sigma)**2)
            t.append([int(k)-1,float(sigma)**2,float('nan'),float('nan'),float(mass),{'KDtree':2,'Nystrom':5,'None':1}[policy.strip()],float('nan'),float('nan'),float('nan'),0])
        stages=[]
        d=dict(status=outcome['kind'],termination='none',observed_iterations=len(t),nystrom_iterations=sum(r[5]==5 for r in t))
        if outcome['kind']=='native_result':
            info=dict(line.split() for line in (folder/(name+'_info.txt')).read_text().splitlines())
            loops=int(info['loops']);assert len(t)==loops and loops<=CAP
            regular=loops<CAP;assert not regular or loops>30
            d.update(status='success',termination='converged' if regular else 'iteration_cap',iterations=loops+int(regular),sigma2=float(info['sigma'])**2,scale=float(np.loadtxt(folder/(name+'_s.txt'))),kernel_rank=100)
            d['final_matched_mass']=float(info['N_hat'])
            assert all(math.isfinite(d[k]) for k in ('sigma2','scale','final_matched_mass'))
            p=np.loadtxt(folder/(name+'_y.txt'))
        d['loss_observation']='upstream does not expose NLL/ELBO; sigma and matched mass only'
    else:
        stderr=folder/(name+'.stderr')
        assert stderr.exists() or outcome['kind']=='unevaluated'
        t,stages=traces(stderr) if stderr.exists() else ([],[])
        d=dict(status=outcome['kind'],termination='none',observed_iterations=len(t),nystrom_iterations=sum(r[5]==5 for r in t))
        if outcome['kind']=='native_result':
            d=json.loads((folder/(name+'.json')).read_text())
            assert d['fixture']==c['fixture'] and d['seed']==c['seed'] and d['points']==N and d['threads']==8
            assert d['variant']==VARIANTS[c['arm'].split('_',1)[1]] and d['landmarks']==256 and d['error_limit']==.001
            assert d['observed_iterations']==len(t) and d['nystrom_iterations']==sum(r[5]==5 for r in t)
            assert all(math.isfinite(v) for r in t for v in r)
            if d['status']=='success':
                assert d['iterations']==len(t) and d['termination'] in ('converged','iteration_cap','sigma_floor')
                assert d['termination']!='iteration_cap' or len(t)==CAP
                p=np.fromfile(folder/(name+'.bin'),dtype='<f8').reshape(N,3)
                assert all(math.isfinite(d[k]) for k in ('sigma2','scale','rms_to_truth'))
                truth=np.loadtxt(INPUT/f"{c['fixture']}_{c['seed']}_truth.txt")
                assert abs(distances(p,truth)[0]-d['rms_to_truth'])<1e-10
            else:
                assert d['iterations']==0 and d['termination']=='none' and not (folder/(name+'.bin')).exists()
    assert [r[0] for r in t]==list(range(len(t))) and len(t)<=CAP
    if p is not None: assert p.shape==(N,3) and np.isfinite(p).all()
    d['regular_convergence']=d['status']=='success' and d['termination']=='converged'
    d['local_iterations']=sum(bool(r[9]) for r in t) if not upstream else None
    d['policy_counts']={str(policy):sum(r[5]==policy for r in t) for policy in (1,2,4,5)}
    d['endpoint_available']=p is not None
    rss=folder/(name+'.rss')
    d['peak_rss_kib']=int(rss.read_text().splitlines()[-1]) if rss.exists() and rss.read_text().splitlines() and rss.read_text().splitlines()[-1].isdigit() else None
    return d,t,stages,p

def metrics(p,data,initial,reference=None):
    source,truth=data['source'].astype(float),data['truth'].astype(float)
    rms,maximum=distances(p,truth);identity=distances(source,truth)[0]
    initialized=distances(initial,truth)[0]
    spread=lambda x:float(np.sqrt(np.mean(np.sum((x-x.mean(0))**2,axis=1))))
    ratio=spread(p)/spread(truth)
    row=dict(truth_rms=rms,truth_max=maximum,identity_rms=identity,initialized_rms=initialized,truth_reduction=1-rms/identity,initialized_reduction=1-rms/initialized,spread_ratio=ratio,truth_screen=rms<=.9*min(identity,initialized) and .5<=ratio<=2)
    if reference is not None:
        a,b=distances(p,reference);row.update(rms_to_auto=a,max_to_auto=b,auto_agreement=quality(a,b))
    return row

def verdict(values):
    return 'REFUTED' if any(x is False for x in values) else 'SUPPORTED' if values and all(x is True for x in values) else 'UNEVALUATED'

def report(folder=None,plan=None):
    import matplotlib;matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    folder=folder or OUT;plan=plan or cells()
    outcomes=json.loads((folder/'outcomes.json').read_text());binding=json.loads((HERE/'input-binding.json').read_text())
    loaded={};invalid={};rows={};parts=[];replays={}
    for c in plan:
        if c['name'] not in outcomes:continue
        try:loaded[c['name']]=load(c,folder,outcomes[c['name']])
        except (AssertionError,ValueError,KeyError,OSError) as e:invalid[c['name']]=type(e).__name__+': '+str(e)
    for c in plan:
        name=c['name']
        if name not in loaded:continue
        d,t,stages,p=loaded[name];row=dict(d);frame=binding[f"{c['fixture']}_{c['seed']}"]['frame']
        world=lambda q:q*frame['scale']+np.array(frame['target_mean'])
        with np.load(HERE.parent/'METHOD-082/fixtures'/f"{c['fixture']}_{c['seed']}.npz") as raw:
            if p is not None:
                p=world(p);refname=f"{c['fixture']}_{c['seed']}_"+('adapted_auto' if c['arm'].startswith('adapted') else 'baseline_auto')
                ref=loaded.get(refname);ref=world(ref[3]) if ref and ref[3] is not None else None
                initial=world(np.loadtxt(INPUT/f"{c['fixture']}_{c['seed']}_source.txt"))
                row.update(metrics(p,raw,initial,ref))
                truth=raw['truth'].astype(float);clutter=np.arange(N)%20==0;partial=c['fixture']=='partial'
                ambiguous=(np.abs(truth[:,0]-.2)<=1e-5)&~clutter if partial else np.zeros(N,dtype=bool)
                observed=~clutter&~ambiguous;hidden=np.zeros(N,dtype=bool)
                if partial:observed &= truth[:,0]<.2;hidden=~clutter&~ambiguous&(truth[:,0]>.2)
                masks=dict(observed=observed,hidden=hidden,clutter_replaced=clutter,boundary_ambiguous=ambiguous)
                row['subsets']={k:dict(count=int(v.sum()),rms=distances(p[v],truth[v])[0] if v.any() else None) for k,v in masks.items()}
                assert sum(v['count'] for v in row['subsets'].values())==N
                if c['arm']=='adapted_union':
                    ref=loaded.get(f"{c['fixture']}_{c['seed']}_upstream_nystrom")
                    if ref and ref[3] is not None:
                        original_rms=distances(world(ref[3]),truth)[0]
                        row['truth_vs_upstream']=row['truth_rms'] <= 1.01*original_rms+2e-6
                        row['upstream_truth_rms']=original_rms
            if name.endswith('_repeat'):
                first=loaded.get(name.removesuffix('_repeat'));replays[name]=None
                if p is not None and first and first[3] is not None:
                    # Six-decimal upstream endpoints; compare all observed fields except elapsed time.
                    columns=[0,1,2,3,4,5,6,7,9]
                    equal_trace=np.array_equal(np.asarray(t).reshape(-1,10)[:,columns],np.asarray(first[1]).reshape(-1,10)[:,columns],equal_nan=True)
                    equal_final=all(d.get(k)==first[0].get(k) for k in ('status','termination','iterations','sigma2','scale','final_matched_mass'))
                    replays[name]=bool(np.array_equal(loaded[name][3],first[3]) and equal_trace and equal_final)
            rows[name]=row
            image=folder/(name+'.png');points_image=folder/(name+'-positions.png')
            if not image.exists():
                fig,axes=plt.subplots(3,2,figsize=(12,10))
                if t:
                    a=np.asarray(t)
                    for ax,col,label in [(axes[0,0],1,'Sigma² (solver units², post update)'),(axes[0,1],4,'Matched mass (before update)'),(axes[1,0],5,'Policy: 1 Dense, 2 KD, 4 IFGT, 5 Nyström'),(axes[1,1],6,'Accepted sampled error'),(axes[2,0],2,'NLL weight 1; stopping objective'),(axes[2,1],7,'Kernel evaluations')]:
                        if np.isfinite(a[:,col]).any():ax.plot(a[:,0],a[:,col])
                        else:ax.text(.1,.5,'Not exposed by upstream',transform=ax.transAxes)
                        ax.set(title=label,xlabel='Observed M-step index')
                        for j in np.flatnonzero(np.diff(a[:,5]))+1:ax.axvline(a[j,0],color='red',linestyle=':')
                    if not c['arm'].startswith('upstream'):
                        axes[2,0].plot(a[:,0],a[:,3],label='objective');axes[2,0].plot(a[:,0],a[:,3]-a[:,2],label='objective minus NLL');axes[2,0].legend()
                fig.tight_layout();fig.savefig(image,dpi=100);plt.close(fig)
            stage_image=folder/(name+'-stages.png')
            if stages and not stage_image.exists():
                fig,ax=plt.subplots(figsize=(12,3));labels=list(dict.fromkeys(s[0] for s in stages))
                ax.scatter([s[1] for s in stages],[labels.index(s[0]) for s in stages],s=3)
                ax.set(yticks=range(len(labels)),yticklabels=labels,xlabel='Elapsed wall seconds',title='Observed stage events')
                fig.tight_layout();fig.savefig(stage_image,dpi=100);plt.close(fig)
            if p is not None and not points_image.exists():
                ids=np.r_[0:100,N-100:N];fig=plt.figure(figsize=(12,5));target=raw['target'][np.r_[0:100,len(raw['target'])-100:len(raw['target'])]]
                clouds=[raw['source'][ids],p[ids],raw['truth'][ids],target];lo=np.vstack(clouds).min(0);hi=np.vstack(clouds).max(0)
                for i,(cloud,label) in enumerate(zip(clouds[:2],('Before (world)','After (world)')),1):
                    ax=fig.add_subplot(1,2,i,projection='3d')
                    for q,lab in ((target,'target'),(raw['truth'][ids],'truth'),(cloud,'source')):ax.scatter(*q.T,s=3,label=lab)
                    ax.set(title=label,xlim=(lo[0],hi[0]),ylim=(lo[1],hi[1]),zlim=(lo[2],hi[2]));ax.legend()
                fig.tight_layout();fig.savefig(points_image,dpi=100);plt.close(fig)
        parts += [f'<h2>{name}</h2><pre>{html.escape(json.dumps(row,indent=2))}</pre><img src="{image.name}">']
        if stages:parts += [f'<img src="{stage_image.name}">']
        if p is not None:parts += [f'<img src="{points_image.name}">']
    primary=[c for c in plan if not c['name'].endswith('_repeat')]
    adapted=[rows.get(c['name'],{}) for c in primary if c['arm'].startswith('adapted_')]
    agreement=[False if r.get('status')=='numerical_failure' else (r['regular_convergence'] and r.get('auto_agreement')) if r.get('endpoint_available') else None for r in adapted]
    summary=dict(complete=len(outcomes)==len(plan),claim_eligible=False,adapted_convergence_and_auto=verdict(agreement),truth_screen_by_arm={a:verdict([rows.get(c['name'],{}).get('truth_screen') for c in primary if c['arm']==a]) for a in ARMS},adapted_union_vs_upstream=verdict([rows.get(c['name'],{}).get('truth_vs_upstream') for c in primary if c['arm']=='adapted_union']),replays=replays,invalid_cells=invalid,technical_errors={k:v['reason'] for k,v in outcomes.items() if v['kind']=='technical_failure'},rows=rows,outcomes=outcomes)
    grouped={}
    for fixture in ('full','partial'):
        for arm in ARMS:
            selected=[rows[c['name']] for c in primary if c['fixture']==fixture and c['arm']==arm and c['name'] in rows]
            values=[r['truth_rms'] for r in selected if 'truth_rms' in r]
            grouped[fixture+'_'+arm]=dict(converged=sum(r['regular_convergence'] for r in selected),cases=len(selected),endpoints=len(values),truth_rms_median=float(np.median(values)) if values else None,truth_rms_min=min(values) if values else None,truth_rms_max=max(values) if values else None)
    summary['groups']=grouped
    table='<table><tr><th>Case / arm</th><th>Native converged / accounted</th><th>Endpoints</th><th>Truth RMS median [min, max], world units</th></tr>'
    for label,g in grouped.items():
        metric='unavailable' if not g['endpoints'] else f"{g['truth_rms_median']:.6g} [{g['truth_rms_min']:.6g}, {g['truth_rms_max']:.6g}]"
        table+=f"<tr><td>{label}</td><td>{g['converged']}/{g['cases']}</td><td>{g['endpoints']}</td><td>{metric}</td></tr>"
    table+='</table>'
    (folder/'summary.json').write_text(json.dumps(summary,indent=2,allow_nan=False)+'\n')
    intro='''<!doctype html><meta charset="utf-8"><title>METHOD-083 original comparison</title><style>body{font:15px system-ui;max-width:1200px;margin:30px auto}img{max-width:100%}pre{white-space:pre-wrap}td,th{padding:5px;border-bottom:1px solid #ccc;text-align:left}</style><h1>Original-inspired Nyström: full solver comparison</h1><p>100k sources; full/partial, three paired fixture seeds. Old/adapted Auto, FPS and box; adapted union-random; unmodified upstream Nyström+KD and direct+KD. All endpoints use the same target world frame. Native stops differ: engine relative objective 1e-5, upstream absolute sigma/change 1e-5 after its minimum loop count. Both cap5000. No trajectory-equivalence or timing claim.</p><p>Frozen screens: adapted regular convergence and RMS/max distance to same-version Auto ≤.001/.01 world units; registration truth RMS improves ≥10% over both original source and the separately centered native initialization, and spread ratio is .5–2; adapted union truth RMS ≤1.01× upstream Nyström RMS +2e-6 rounding margin. Failures and timeouts remain distinct; a known counterexample refutes a universal screen even when other cells are unevaluated.</p><p>Engine NLL/objective/mass precede M-step, sigma follows it; final convergence-only E-step is unobserved. Upstream prints sigma/mass/backend only and omits its final converging step; info.txt contains its endpoint sigma. NLL/ELBO decomposition, loss shares and original trajectory coordinates are unavailable from this unmodified command and are not reconstructed or treated as passes. For Bayesian runs the engine logs NLL with weight1 (raw=weighted, share100% of the logged stopping objective); objective minus NLL is zero. No full variational loss decomposition is exposed. Stage-event timelines show all exposed phase events; upstream phase events are unavailable. Upstream J changes consume random draws before its G basis is sampled, so its direct and Nyström arms are complete native variants, not an isolated E-step ablation. Red lines mark observed backend transitions. Pictures show first/last100 points; metrics use all points. Three seeds are cases, not population confidence; replays check repeatability only.</p>'''
    (folder/'report.html').write_text(intro+table+'<pre>'+html.escape(json.dumps({k:v for k,v in summary.items() if k not in ('rows','outcomes','groups')},indent=2))+'</pre>'+''.join(parts))
    return summary

async def run_async():
    expected=hashlib.sha256((HERE/'preflight/manifest.txt').read_bytes()).hexdigest()
    assert json.loads((HERE/'reviews/approval.json').read_text())==dict(digest=expected,codex_a='PASS',codex_b='PASS')
    OUT.mkdir(exist_ok=False);deadline=time.monotonic()+TOTAL_SECONDS;queue=asyncio.Queue()
    for slot in range(WORKERS):queue.put_nowait(slot)
    outcomes={};active=set();tasks=[];stopped=asyncio.Event()
    async def cell(c):
        slot=await queue.get();name=c['name'];proc=None
        try:
            if stopped.is_set():return name,dict(kind='unevaluated',reason='technical stop')
            assert hashlib.sha256(subprocess.check_output(manifest_command(),cwd=ROOT)).hexdigest()==expected,'frozen inputs changed'
            remaining=deadline-time.monotonic()
            if remaining<=0:return name,dict(kind='unevaluated',reason='total budget exhausted')
            cmd=command(c,OUT,slot);(OUT/(name+'.command.json')).write_text(json.dumps(cmd,indent=2)+'\n');print('START',name,flush=True)
            with (OUT/(name+'.stdout')).open('w') as so,(OUT/(name+'.stderr')).open('w') as se:
                proc=await asyncio.create_subprocess_exec(*cmd,cwd=ROOT,stdout=so,stderr=se,start_new_session=True,env=dict(os.environ,OMP_NUM_THREADS='8',OPENBLAS_NUM_THREADS='1'))
                active.add(proc)
                try:code=await asyncio.wait_for(proc.wait(),timeout=min(CELL_SECONDS,remaining))
                except asyncio.TimeoutError:
                    os.killpg(proc.pid,signal.SIGKILL);await proc.wait();return name,dict(kind='unevaluated',reason='native timeout')
                if code==124:return name,dict(kind='unevaluated',reason='native timeout')
                if code==0:outcome=dict(kind='native_result',exit_code=code)
                elif code==1 and not c['arm'].startswith('upstream'):
                    outcome=dict(kind='native_result',exit_code=code)
                elif code==1 and re.search(r'ERROR: (Np became negative|The Cholesky factorization of G failed|Inversion of G failed|LU decomp|The Cholesky factorization failed at gaussprod|Solving linear equations failed at gaussprod|The approximate eigendecomposition failed)',(OUT/(name+'.stdout')).read_text()):
                    outcome=dict(kind='numerical_failure',exit_code=code)
                else:raise RuntimeError(f'{name}: unexpected native exit {code}')
            load(c,OUT,outcome) # validate before releasing the slot for another cell
            return name,outcome
        except Exception as error:
            outcomes[name]=dict(kind='technical_failure',reason=type(error).__name__+': '+str(error))
            stopped.set()
            raise
        finally:
            if proc:active.discard(proc)
            queue.put_nowait(slot)
    try:
        tasks=[asyncio.create_task(cell(c)) for c in cells()]
        for completed in asyncio.as_completed(tasks):
            name,outcome=await completed;outcomes[name]=outcome
            (OUT/'outcomes.json').write_text(json.dumps(outcomes,indent=2)+'\n')
            result=await asyncio.to_thread(report);assert not result['invalid_cells'],'invalid measurement report'
            print('DONE',name,outcome['kind'],flush=True)
    finally:
        for proc in active:
            if proc.returncode is None:
                try:os.killpg(proc.pid,signal.SIGKILL)
                except ProcessLookupError:pass
        for t in tasks:
            if not t.done():t.cancel()
        await asyncio.gather(*tasks,return_exceptions=True)
        for c in cells():outcomes.setdefault(c['name'],dict(kind='unevaluated',reason='technical stop'))
        (OUT/'outcomes.json').write_text(json.dumps(outcomes,indent=2)+'\n');report()

if __name__=='__main__':
    if not __debug__:raise RuntimeError('Assertions required')
    if sys.argv[1]=='run':asyncio.run(run_async())
    elif sys.argv[1]=='report':report()
    else:raise ValueError('run or report required')

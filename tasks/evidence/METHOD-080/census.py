#!/usr/bin/env python3
"""Bounded descriptive census; complete native IDs, explicitly sampled exact row diagnostics."""
if not __debug__:
    raise RuntimeError('Optimized Python is forbidden')
import csv, hashlib, html, importlib.util, json, math, os, re, resource, signal, struct, subprocess, sys, time
from pathlib import Path
import numpy as np
from scipy.spatial import cKDTree
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
OLD=HERE.parent/'METHOD-079'
OUT=HERE/'runs'
BIN=Path('/tmp/cpd-nystrom-census-20261011/bin/native')
PLAN=[('points_1','points/fixed/random',7),('box_1','box/resampled/random',2),('box_2','box/resampled/random',2),('points_2','points/fixed/random',7)]
R=np.longdouble
spec=importlib.util.spec_from_file_location('traces078',HERE.parent/'METHOD-078/diagnose.py')
trace=importlib.util.module_from_spec(spec);spec.loader.exec_module(trace)

def dump(path,value):
    path.write_text(json.dumps(value,indent=2,allow_nan=False)+'\n')

def finite(value):
    return float(value) if np.isfinite(value) else str(value)

def snapshot(path):
    raw=path.read_bytes();magic,L,M,j,draw=struct.unpack_from('<5Q',raw)
    assert magic==79 and 1<=L<=4096 and M==100000 and j<M
    v=np.frombuffer(raw,dtype='<f8',offset=40);off=0
    def take(n):
        nonlocal off
        a=v[off:off+n];assert len(a)==n;off+=n;return a
    s=take(7);z=take(3*L).reshape(3,L).T;y=take(3*M).reshape(3,M).T;lw=take(M)
    W=take(L*L).reshape(L,L,order='F');d=take(L);U=take(L*L).reshape(L,L,order='F')
    mask=take(L);kx=take(L);a=take(L);g=take(L);P=take(L*L).reshape(L,L,order='F');assert off==len(v)
    assert np.isfinite(s[:6]).all() and s[0]>0 and np.isfinite(z).all() and np.isfinite(y).all() and np.isfinite(lw).all()
    return dict(L=L,M=M,j=j,draw=draw,s=s,z=z,y=y,lw=lw,a=a,g=g,kx=kx)

def rows(path):
    raw=path.read_bytes();magic,n,draw,L=struct.unpack_from('<4Q',raw)
    assert magic==80 and n==100000 and 1<=L<=4096 and len(raw)==56+9*n
    meta=np.frombuffer(raw,dtype='<f8',count=3,offset=32)
    value=np.frombuffer(raw,dtype='<f8',count=n,offset=56)
    visited=np.frombuffer(raw,dtype='u1',count=n,offset=56+8*n)
    assert np.all(visited==1),'unvisited or multiply visited rows'
    return n,draw,L,meta,value

def exact(x,y,lw,sigma2):
    # The log result remains valid when exponentiating the row sum underflows.
    delta=y.astype(R)-np.asarray(x,dtype=R)
    logs=np.asarray(lw,dtype=R)-np.sum(delta*delta,axis=1)/(2*R(sigma2))
    top=logs.max();return top+np.log(np.exp(logs-top).sum())

def mass(logs,shift,logc):
    v=logs+R(shift)-R(logc)
    return 1/(1+np.exp(-v)) if v>=0 else np.exp(v)/(1+np.exp(v))

def choose(ids,cap,seed):
    return ids if len(ids)<=cap else np.sort(np.random.default_rng(seed).choice(ids,cap,replace=False))

def stats(v):
    return None if len(v)==0 else dict(zip(['min','median','p95','max'],map(float,np.quantile(v,[0,.5,.95,1]))))

def analyze(folder,name):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    targets=np.fromfile(folder/(name+'.snapshot.targets'),dtype='<f8').reshape(3,-1).T
    assert targets.shape==(100000,3) and np.isfinite(targets).all()
    neighbour=cKDTree(targets).query(targets,k=2,workers=8)[0][:,1]
    labels=np.arange(len(targets))%20==0
    attempts=[]
    files=sorted(folder.glob(name+'.snapshot.draw*.rows'),key=lambda p:(int(p.name.split('.draw')[1].split('.')[0]),int(p.name.split('.L')[1].split('.')[0])))
    assert files
    log=(folder/(name+'.stderr')).read_text()
    spectra={(int(d),int(l)) for d,l in re.findall(r'^NYSTROM_SPECTRUM (\d+) (\d+) ',log,re.M)}
    decisions={(int(d),int(l)):(int(ok),float(error)) for d,l,ok,error in re.findall(r'^NYSTROM_DIAG attempt (\d+) (\d+) (\d+) (\S+)',log,re.M)}
    rejected={(int(d),int(l)) for d,l in re.findall(r'^NYSTROM_DIAG invalid_target (\d+) (\d+)',log,re.M)}
    captured={(rows(p)[1],rows(p)[2]) for p in files}
    assert captured==spectra==set(decisions),'attempt coverage or unexpected pre-target failure'
    for path in files:
        n,draw,L,meta,S=rows(path);negative=np.isfinite(S)&(S<0);zero=np.isfinite(S)&(S==0);nonfinite=~np.isfinite(S);bad=negative|zero|nonfinite;positive=np.isfinite(S)&(S>0)
        item=dict(draw=draw,landmarks=L,sigma2=float(meta[0]),negative=int(negative.sum()),zero=int(zero.sum()),nonfinite=int(nonfinite.sum()),positive=int(positive.sum()),negative_clutter=int((negative&labels).sum()),negative_regular=int((negative&~labels).sum()),invalid_ids=np.flatnonzero(bad).tolist())
        item.update(negative_clutter_rate=item['negative_clutter']/5000,negative_regular_rate=item['negative_regular']/95000)
        assert bool(bad.any())==((draw,L) in rejected)
        if bad.any():assert decisions[draw,L][0]==0,'invalid attempt was accepted'
        item.update(accepted=bool(decisions[draw,L][0]),sampled_relative_error=finite(decisions[draw,L][1]))
        operand=path.with_suffix('.snapshot')
        if not bad.any():
            assert not operand.exists();attempts.append(item);continue
        assert operand.exists();v=snapshot(operand);assert v['draw']==draw and v['L']==L and np.array_equal(v['s'][:3],meta)
        j=int(np.flatnonzero(bad)[0]);assert j==v['j'] and struct.pack('<d',S[j])==struct.pack('<d',v['s'][6]);assert np.array_equal(targets[j],v['s'][3:6])
        sigma=math.sqrt(meta[0]);source=cKDTree(v['y']).query(targets,workers=8)[0]/sigma
        landmark=cKDTree(v['z']).query(targets,workers=8)[0]/sigma
        geometry=np.column_stack([source,neighbour/sigma,landmark])
        item.update(g_negative=int((v['g']<0).sum()),a_negative=int((v['a']<0).sum()),invalid_coincident_landmark=int((bad&(landmark==0)).sum()),geometry={})
        for group,mask in [('negative_clutter',negative&labels),('negative_regular',negative&~labels),('positive_clutter',positive&labels),('positive_regular',positive&~labels)]:
            item['geometry'][group]={key:stats(geometry[mask,c]) for c,key in enumerate(['source_sigma','neighbour_sigma','landmark_sigma'])}
        stem=f'{name}.draw{draw}.L{L}'
        with (folder/(stem+'.invalid.csv')).open('w') as f:
            w=csv.writer(f);w.writerow(['target_index','x_normalized','y_normalized','z_normalized','injected_clutter','native_shifted_sum','nearest_source_sigma','nearest_other_target_sigma','nearest_landmark_sigma'])
            for i in np.flatnonzero(bad):w.writerow([i,*targets[i],int(labels[i]),S[i],*geometry[i]])
        invalid=np.flatnonzero(bad);seed=8001+draw*10000+L
        chosen=choose(invalid,1024,seed);examples=[j]
        if negative.any():examples.append(int(np.flatnonzero(negative)[np.argmin(S[negative])]))
        chosen=np.unique(np.r_[chosen,examples]);valid=choose(np.flatnonzero(positive),256,seed+1)
        selected=np.r_[chosen,valid];records=[]
        for i in selected:
            loge=exact(targets[i],v['y'],v['lw'],meta[0]);p=mass(loge,meta[2],meta[1])
            delta=v['z'].astype(R)-targets[i].astype(R);k=np.exp(-np.sum(delta*delta,axis=1)/(2*R(meta[0])));terms=k*v['g'].astype(R)
            plus=terms[terms>=0].sum();minus=terms[terms<0].sum();dot=terms.sum();absolute=np.abs(terms).sum()
            records.append([int(i),int(bad[i]),int(labels[i]),finite(S[i]),float(loge),finite(np.exp(loge)),float(p),finite(plus),finite(minus),finite(dot),finite(absolute/abs(dot)) if dot!=0 else 'inf',float(loge+R(meta[2])),bool(np.sign(dot)==np.sign(S[i]))])
        fields=['target_index','invalid','injected_clutter','native_shifted_sum','exact_log_shifted_sum','exact_shifted_sum','exact_inlier_mass','positive_landmark_dot_terms','negative_landmark_dot_terms','rebuilt_kernel_saved_g_dot','landmark_cancellation_ratio','exact_unshifted_log_affinity','same_sign']
        with (folder/(stem+'.exact-sample.csv')).open('w') as f:
            w=csv.writer(f);w.writerow(fields);w.writerows(records)
        item.update(exact_invalid_sample=len(chosen),exact_invalid_population=len(invalid),exact_valid_sample=len(valid),exact_valid_population=int(positive.sum()),sampling_seed=seed,examples=[dict(zip(fields,row)) for row in records if row[0] in examples],sample_summary={})
        for group in [0,1]:
            for label in [0,1]:
                group_rows=[row for row in records if row[1]==group and row[2]==label]
                item['sample_summary'][f'invalid{group}_clutter{label}']={'count':len(group_rows),'exact_log_sum':stats([r[4] for r in group_rows]),'inlier_mass':stats([r[6] for r in group_rows]),'rebuilt_sign_disagreements':sum(not r[-1] for r in group_rows)}
        # Every geometric summary is within one attempt, with the same moved source and sigma.
        fig,axes=plt.subplots(1,3,figsize=(15,4))
        sample=choose(np.arange(n),3000,8080)
        for ax,c in zip(axes[:2],[1,2]):
            ax.scatter(targets[sample,0],targets[sample,c],s=2,c='0.7',label='target sample')
            yi=choose(np.arange(n),2000,8081);ax.scatter(v['y'][yi,0],v['y'][yi,c],s=2,label='moved source sample')
            ax.scatter(targets[bad,0],targets[bad,c],s=9,c='red',label='all invalid targets')
            ax.set(xlabel='normalized x',ylabel='normalized '+('y' if c==1 else 'z'));ax.legend(fontsize=7)
        axes[2].hist(source[positive&labels],bins=40,density=True,alpha=.5,label='positive clutter')
        if (negative&labels).any():axes[2].hist(source[negative&labels],bins=40,density=True,alpha=.5,label='negative clutter')
        axes[2].set(xlabel='Nearest moved-source distance / sigma',ylabel='Density');axes[2].legend(fontsize=7)
        fig.suptitle(f'{name}: draw {draw}, L={L}; red markers show every invalid row');fig.tight_layout();fig.savefig(folder/(stem+'.png'));plt.close(fig)
        # Frozen real-data oracle/replay controls, not an adaptive scientific threshold.
        if (draw,L)==((3,256) if name.startswith('points') else (1,512)):
            assert operand.read_bytes()==(OLD/'runs'/(name+'.snapshot')).read_bytes()
            old=json.loads((OLD/'runs/summary.json').read_text())[name]
            assert abs(math.expm1(records[0][4]-old['exact_log_sum_shifted']))<1e-12
        attempts.append(item)
    dump(folder/(name+'.census.json'),attempts)

def native_command(cell,folder,binary=BIN):
    name,variant,iterations=cell
    return ['taskset','-c','0-7',str(binary),'--fixture','full','--seed','1','--points','100000','--threads','8','--iterations',str(iterations),'--trace','1','--variant',variant,'--landmarks','256','--error-limit','0.001','--out',str(folder/(name+'.json')),'--positions',str(folder/(name+'.bin'))]

def limits():
    resource.setrlimit(resource.RLIMIT_AS,(8<<30,8<<30));resource.setrlimit(resource.RLIMIT_CORE,(0,0))

def report(folder):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    body=['<h1>METHOD-080: all invalid target rows in the observed prefixes</h1><p>Descriptive CPU census; no causal sampler comparison, production remedy or speed claim. Points use farthest-point sampling, fixed identities; boxes use uniform random sampling, resampled each draw. All coordinates are normalized. Injected clutter is a fixture label, distinct from posterior inlier mass.</p>']
    failed=(folder/'failure.json').exists();complete=(folder/'completion.json').exists()
    body+=[f'<p>{"UNEVALUATED: technical failure" if failed else "Complete" if complete else "INCOMPLETE: pending cells"}</p>']
    if failed:body+=['<pre>'+html.escape((folder/'failure.json').read_text())+'</pre>']
    preview={k:[] for k in ['source','target','truth']}
    for line in (OLD.parent/'METHOD-078/fixture-preview.txt').read_text().splitlines():
        m=re.match(r'(source|target|truth)\[(\d+)\] (.*)',line)
        if m:preview[m[1]].append((int(m[2]),tuple(map(float,m[3].split()))))
    for name,_,_ in PLAN:
        done=folder/(name+'.analysis.exit.json');body+=[f'<h2>{name}</h2>']
        if not done.exists() or json.loads(done.read_text())['returncode']!=0:
            body+=['<p>UNEVALUATED: analysis not completed.</p>'];continue
        body+=['<p>All counts/IDs are complete. Exact masses use all invalid rows up to 1024, otherwise a seeded uniform sample plus first/most-negative examples; 256 uniformly sampled valid rows are the comparison. Quantiles refer to the stated population or sample, never to other draws.</p>']
        for item in json.loads((folder/(name+'.census.json')).read_text()):
            draw,L=item['draw'],item['landmarks'];stem=f'{name}.draw{draw}.L{L}';ids=item['invalid_ids'];shown=dict(item);shown.pop('invalid_ids')
            body+=[f'<h3>Draw {draw}, L={L}</h3><pre>'+html.escape(json.dumps(shown,indent=2))+'</pre>']
            if ids:body+=[f'<p><a href="{stem}.invalid.csv">Every invalid ID and geometry</a> · <a href="{stem}.exact-sample.csv">Exact sample and signed landmark-dot terms</a></p><img src="{stem}.png">']
        its,_,_,_,stages=trace.traces(folder/(name+'.stderr'));v=np.asarray(its);fig,axes=plt.subplots(2,2,figsize=(11,6))
        axes[0,0].plot(v[:,0],v[:,2],label='NLL raw=weighted (weight 1)');axes[0,0].plot(v[:,0],v[:,3],ls='--',label='stopping objective');axes[0,0].plot(v[:,0],v[:,3]-v[:,2],label='extra prior=0');axes[0,0].legend(fontsize=7)
        axes[0,1].plot(v[:,0],v[:,1]);axes[0,1].set_title('Post-M-step sigma squared, world units squared')
        axes[1,0].plot(v[:,0],v[:,5]);axes[1,0].set_title('Actual E-step policy (5 Nystrom, 1 dense)')
        names=list(dict.fromkeys(s for s,t in stages));axes[1,1].step([t for s,t in stages],[names.index(s) for s,t in stages],where='post');axes[1,1].set_yticks(range(len(names)),names,fontsize=7);axes[1,1].set_xlabel('Seconds; no timing comparison')
        fig.tight_layout();fig.savefig(folder/(name+'-trace.png'));plt.close(fig)
        body+=[f'<img src="{name}-trace.png"><p>NLL share = 1; extra prior share = 0 in the implemented stopping objective, not a full ELBO. E-step state and post-M-step sigma are distinct.</p><pre>'+html.escape('\n'.join(x for x in (folder/(name+'.stderr')).read_text().splitlines() if x.startswith('NYSTROM_')))+'</pre>']
        endpoints=np.fromfile(folder/(name+'.bin'),dtype='<f8').reshape(-1,3);idx=[i for i,p in preview['source']];before=np.array([p for i,p in preview['source']]);target=np.array([p for i,p in preview['target']]);truth=np.array([p for i,p in preview['truth']]);after=endpoints[idx];cloud=np.concatenate([before,after,target,truth]);lo=cloud.min(0);hi=cloud.max(0)
        fig=plt.figure(figsize=(10,4))
        for pos,(pts,title) in enumerate([(before,'Before'),(after,'After bounded prefix')],1):
            ax=fig.add_subplot(1,2,pos,projection='3d')
            for data,label in [(target,'target'),(truth,'truth'),(pts,'source')]:ax.scatter(*data.T,s=2,label=label)
            ax.set(xlim=(lo[0],hi[0]),ylim=(lo[1],hi[1]),zlim=(lo[2],hi[2]),title=title);ax.legend(fontsize=7)
        fig.tight_layout();fig.savefig(folder/(name+'-positions.png'));plt.close(fig);body+=[f'<img src="{name}-positions.png">']
    if complete and not failed:
        for stem in ['points','box']:
            a=json.loads((folder/(stem+'_1.census.json')).read_text());b=json.loads((folder/(stem+'_2.census.json')).read_text())
            same=all(p.read_bytes()==p.with_name(p.name.replace(stem+'_1',stem+'_2',1)).read_bytes() for p in folder.glob(stem+'_1.snapshot*'))
            body+=[f'<p>{stem}: repeat numerical summaries identical = {a==b}; all native buffers/snapshots byte-identical = {same}</p>']
    (folder/'report.html').write_text('<!doctype html><meta charset="utf-8"><style>body{font-family:system-ui;max-width:1250px;margin:auto}img{max-width:100%}pre{white-space:pre-wrap}</style>'+''.join(body))

def run():
    packet=HERE/'packet.md';approved=json.loads((HERE/'reviews/approval.json').read_text());head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    assert approved['verdict']=='PASS' and approved['commit']==head and approved['packet_sha256']==hashlib.sha256(packet.read_bytes()).hexdigest()
    OUT.mkdir(exist_ok=False);dump(OUT/'started.json',approved)
    (OUT/'report.html').write_text('<h1>METHOD-080</h1><p>INCOMPLETE: raw outputs retained, no completed analysis.</p>')
    def expired(signum,frame):raise TimeoutError('1200-second total budget or termination')
    signal.signal(signal.SIGALRM,expired);signal.signal(signal.SIGTERM,expired);signal.alarm(1200);start=time.monotonic();name='before-start'
    try:
        for cell in PLAN:
            name=cell[0]
            for phase,cmd,budget in [('native',native_command(cell,OUT),90),('analysis',['taskset','-c','0-7',sys.executable,str(HERE/'census.py'),'analyze',str(OUT),name],180)]:
                assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()==head
                assert not subprocess.check_output(['git','status','--porcelain','--untracked-files=no'],cwd=ROOT),'tracked tree changed'
                for path,digest in approved['inputs'].items():assert hashlib.sha256(Path(path).read_bytes()).hexdigest()==digest,path
                env=dict(os.environ,INTRINSIC_NYSTROM_CAPTURE=str(OUT/(name+'.snapshot')),OPENBLAS_NUM_THREADS='1',OMP_NUM_THREADS='1')
                dump(OUT/(name+'.'+phase+'.command.json'),{'argv':cmd,'capture':env['INTRINSIC_NYSTROM_CAPTURE']})
                with (OUT/(name+('.stdout' if phase=='native' else '.analysis.stdout'))).open('w') as stdout,(OUT/(name+('.stderr' if phase=='native' else '.analysis.stderr'))).open('w') as stderr:
                    r=subprocess.run(cmd,cwd=ROOT,env=env,stdout=stdout,stderr=stderr,preexec_fn=limits,timeout=min(budget,1200-(time.monotonic()-start)))
                dump(OUT/(name+'.'+phase+'.exit.json'),{'returncode':r.returncode});assert r.returncode==0,(name,phase,r.returncode)
                if phase=='native':assert (OUT/(name+'.bin')).stat().st_size==2400000
            report(OUT);print('Completed',name,flush=True)
        dump(OUT/'completion.json',{'cells':[c[0] for c in PLAN],'max_child_rss_kib':resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss});report(OUT)
    except BaseException as e:
        signal.alarm(0);dump(OUT/'failure.json',{'cell':name,'error':str(e)});(OUT/'report.html').write_text('<h1>UNEVALUATED: technical failure</h1><pre>'+html.escape(str(e))+'</pre>');raise
    finally:signal.alarm(0)

if __name__=='__main__':
    if sys.argv[1:2]==['analyze']:analyze(Path(sys.argv[2]),sys.argv[3])
    elif sys.argv[1:]==['run']:run()
    else:raise SystemExit('usage: census.py run | analyze folder cell')

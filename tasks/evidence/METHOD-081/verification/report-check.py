"""Known-answer synthetic report check; no native experiment outcomes read."""
from pathlib import Path
import importlib.util,json,tempfile,contextlib
import types
import numpy as np
HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('compare',HERE.parent/'compare.py');c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
c.self_test()
with tempfile.TemporaryDirectory() as tmp:
 c.OUT=Path(tmp)
 cell=c.cells()[0];name,_,_,k=cell
 # Known artificial output tests the report/trace IO, not registration.
 np.ones((100000,3),dtype='<f8').tofile(c.OUT/(name+'.bin'))
 (c.OUT/(name+'.json')).write_text(json.dumps({'status':'success','termination':'max_iterations','iterations':k,'nystrom_iterations':0,'seconds':1.0}))
 (c.OUT/(name+'.stderr')).write_text('CPD_STAGE expectation 0\n'+''.join(f'CPD_ITER {i} 1 2 2 100 1 0 1000 {i+1}\n' for i in range(k)))
 result=c.report();assert not result['complete'] and result['verdict']=='INCOMPLETE / UNEVALUATED'
 html=(c.OUT/'report.html').read_text();assert 'data:image/png;base64,' in html and 'world units' in html and 'NLL' in html
 # Missing iteration and collapsed/zero/shifted controls must not be accepted as valid.
 (c.OUT/(name+'.stderr')).write_text('CPD_STAGE expectation 0\n')
 try:c.read(cell,c.OUT)
 except AssertionError:pass
 else:raise AssertionError('incomplete trace accepted')
 # A technical failure still produces an unevaluated report without promoting invalid data.
 (c.OUT/'failure.json').write_text(json.dumps({'error':'synthetic incomplete trace'}))
 result=c.report()
 assert not result['complete'] and not result['rows'] and name in result['invalid_cells']
 assert result['verdict']=='INCOMPLETE / UNEVALUATED'
 assert 'synthetic incomplete trace' in (c.OUT/'report.html').read_text()
print('PASS: incomplete known-answer report, embedded charts and trace rejection')
s=(HERE.parent/'compare.py').read_text()
for name,old,new in [('quality','def quality(rms,maximum): return math.isfinite(rms+maximum) and rms<=0.001 and maximum<=0.01','def quality(rms,maximum): return True'),('mechanism','def improved(delta): return min(delta.values())>=0 and max(delta.values())>0','def improved(delta): return True')]:
 assert s.count(old)==1
 ns={'__name__':'mutation','__file__':str(HERE.parent/'compare.py')};exec(compile(s.replace(old,new),'<mutation>','exec'),ns)
 try:ns['self_test']()
 except AssertionError:print('PASS: control rejects',name,'mutation')
 else:raise AssertionError(name+' escaped')

def whole_comparison(module,case):
 """Known-answer ten-cell outputs exercise the actual IO and final conjunction."""
 with tempfile.TemporaryDirectory() as tmp:
  module.OUT=Path(tmp)
  x=np.linspace(-1.,1.,100000)
  reference=np.column_stack((x,np.sin(3*x),np.cos(2*x)))
  for cell in module.cells():
   name,arm,variant,k=cell
   count=0 if variant=='exact' else (2 if arm=='candidate' and variant=='points' else 1)
   if case=='noop' and arm=='candidate':count=1
   if case=='regression' and arm=='candidate' and variant=='box':count=0
   early=case=='early_all' or (case=='early' and arm=='candidate' and variant=='box')
   actual=k-1 if early else k
   p=reference.copy()
   if arm=='candidate' and variant=='points':
    if case=='displaced':p+=0.02
    if case=='collapsed':p[:]=reference.mean(axis=0)
   p.astype('<f8').tofile(module.OUT/(name+'.bin'))
   d={'status':'success','termination':'converged' if early else 'max_iterations',
      'iterations':actual,'nystrom_iterations':count,'seconds':1.0}
   if variant!='exact':d['rms_to_exact'],d['max_to_exact']=module.distances(p,reference)
   (module.OUT/(name+'.json')).write_text(json.dumps(d))
   (module.OUT/(name+'.stderr')).write_text('CPD_STAGE expectation 0\n'+''.join(
    f'CPD_ITER {i} 1 2 2 100 {5 if i<count else 1} 0 1000 {i+1}\n' for i in range(actual)))
  # Real chart encoding is tested above; these cases target complete verdict assembly.
  original=module.figure_url
  module.figure_url=lambda figure:'data:image/png;base64,'
  try:result=module.report()
  finally:module.figure_url=original
  assert result['complete'] and len(result['rows'])==10
  expected='BOUNDED PREDICTION SUPPORTED' if case=='positive' else 'BOUNDED PREDICTION NOT SUPPORTED'
  assert result['verdict']==expected,(case,result['verdict'])
  if case=='early':
   assert not result['rows']['candidate_box_1']['outcome_match']
   assert 'baseline_points_2' in result['rows']
  print('PASS: complete comparison',case)

for case in ('positive','displaced','collapsed','noop','regression','early','early_all'):whole_comparison(c,case)
for name,old,new,case in [
 ('final-quality',"summary['quality_pass'] and summary['repeat_identity']","summary['repeat_identity']",'displaced'),
 ('final-mechanism',"and summary['mechanism_prediction'] else","else",'noop'),
 ('planned-prefix',"d['prefix_complete'] and ","",'early_all')]:
 assert s.count(old)==1
 module=types.ModuleType('mutation');module.__file__=str(HERE.parent/'compare.py')
 exec(compile(s.replace(old,new),'<mutation>','exec'),module.__dict__)
 try:whole_comparison(module,case)
 except AssertionError:print('PASS: complete control rejects',name,'mutation')
 else:raise AssertionError(name+' escaped')

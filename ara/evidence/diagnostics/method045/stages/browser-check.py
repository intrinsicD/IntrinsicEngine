import base64,json,urllib.request,websocket
from pathlib import Path
root=Path('/home/alex/Documents/IntrinsicEngine/build/method045-stage-audit-v2')
tabs=json.load(urllib.request.urlopen('http://localhost:9348/json'))
ws=websocket.create_connection(next(t for t in tabs if t['type']=='page')['webSocketDebuggerUrl'],origin='http://localhost:9348',timeout=60)
seq=0
def command(method,params=None):
 global seq
 seq+=1;ws.send(json.dumps({'id':seq,'method':method,'params':params or {}}))
 while True:
  d=json.loads(ws.recv())
  if d.get('id')==seq:
   if 'error' in d:raise RuntimeError(d)
   return d['result']
def js(source,promise=False):
 r=command('Runtime.evaluate',{'expression':source,'returnByValue':True,'awaitPromise':promise})
 if r.get('exceptionDetails'):raise RuntimeError(r['exceptionDetails'])
 return r['result'].get('value')
command('Page.enable');command('Runtime.enable');command('Page.navigate',{'url':(root/'index.html').as_uri()})
ready=js('new Promise(resolve=>{let n=0;const f=()=>{if(window.viewerInspection)resolve(window.viewerInspection());else if(++n>300)resolve({error:document.getElementById("status")?.textContent});else setTimeout(f,50)};f()})',True)
assert ready.get('status')=='ready',ready
report={'initial':ready,'coverage':[]}
for i in range(2):
 js(f'$("mesh").value="{i}";$("mesh").dispatchEvent(new Event("change"));')
 count=js('selectedMesh.runs.length')
 for start in range(0,count,20):
  records=js(f'(()=>{{const out=[];for(let j={start};j<Math.min({start+20},selectedMesh.runs.length);j++){{$ ("rightMode").value=String(j);$("rightMode").dispatchEvent(new Event("change"));out.push({{name:views[1].run.name,...viewerInspection()}});}}return out;}})()')
  assert all(x['glErrors']==[0,0] for x in records)
  report['coverage'].extend(records)
print('Covered',len(report['coverage']),'views',flush=True)
js('$("mesh").value="0";$("mesh").dispatchEvent(new Event("change"));$("leftMode").value=String(selectedMesh.runs.findIndex(r=>r.name==="feature: Initial 64 seeded clusters"));$("leftMode").dispatchEvent(new Event("change"));$("rightMode").value=String(selectedMesh.runs.findIndex(r=>r.name==="feature: Final charts before native packing"));$("rightMode").dispatchEvent(new Event("change"));')
report['unfiltered']=js('viewerInspection()')
report['filtered']=js('$("curveStrength").value="1000000";$("curveStrength").dispatchEvent(new Event("input"));viewerInspection()')
assert report['filtered']['curveSegments']==[0,0]
js('$("curveStrength").value="0";$("curveStrength").dispatchEvent(new Event("input"));')
command('Emulation.setDeviceMetricsOverride',{'width':1600,'height':1120,'deviceScaleFactor':1,'mobile':False})
js('new Promise(resolve=>requestAnimationFrame(()=>{renderAll();resolve(true)}))',True)
shot=command('Page.captureScreenshot',{'format':'png'});(root/'frog-viewer.png').write_bytes(base64.b64decode(shot['data']))
report['sculpt_large']=js('$("mesh").value="1";$("mesh").dispatchEvent(new Event("change"));$("curveScale").value="2";$("curveScale").dispatchEvent(new Event("input"));viewerInspection()')
assert report['sculpt_large']['curveSegments'][0]>0
(root/'browser-check.json').write_text(json.dumps(report,indent=2))
print(json.dumps({'views':len(report['coverage']),'initial':ready,'filtered':report['filtered']}))
ws.close()

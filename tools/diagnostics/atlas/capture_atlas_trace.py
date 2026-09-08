#!/usr/bin/env python3
"""Capture depth-tested atlas panels using a separate local Chrome debug session.

Requires the optional websocket-client Python package. This connects only to
localhost; start Chrome with a dedicated temporary user-data directory.
"""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import urllib.request

import websocket


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace',type=Path)
    parser.add_argument('--port',type=int,default=9348)
    args=parser.parse_args(); root=args.trace.resolve()
    endpoint=f'http://localhost:{args.port}'
    tabs=json.load(urllib.request.urlopen(endpoint+'/json'))
    ws=websocket.create_connection(next(t for t in tabs if t['type']=='page')['webSocketDebuggerUrl'],origin=endpoint,timeout=60)
    seq=0
    def command(method,params=None):
        nonlocal seq
        seq+=1;ws.send(json.dumps(dict(id=seq,method=method,params=params or {})))
        while True:
            data=json.loads(ws.recv())
            if data.get('id')==seq:
                if 'error' in data:
                    raise RuntimeError(data)
                return data['result']
    def js(source,promise=False):
        result=command('Runtime.evaluate',dict(expression=source,returnByValue=True,awaitPromise=promise))
        if result.get('exceptionDetails'):
            raise RuntimeError(result['exceptionDetails'])
        return result['result'].get('value')
    try:
        command('Page.enable');command('Runtime.enable')
        command('Page.navigate',{'url':(root/'index.html').as_uri()})
        ready=js('new Promise(resolve=>{let n=0;const f=()=>{if(window.viewerInspection)resolve(viewerInspection());else if(++n>300)resolve({error:document.getElementById("status")?.textContent});else setTimeout(f,50)};f()})',True)
        if ready.get('status')!='ready':
            raise RuntimeError(ready)
        command('Emulation.setDeviceMetricsOverride',dict(width=1600,height=1120,deviceScaleFactor=1,mobile=False))
        names=['Native baseline: initial growth','Native baseline: final regions','feature: Initial 64 seeded clusters',
               'feature: Final charts before native packing','protected: Validated charts before merging',
               'protected: Final charts before native packing','Scalar: signed k1','Scalar: signed k2',
               'Scalar: native soft feature confidence','Scalar: max incident dihedral',
               'protected: final local UV stretch','Unused curve scalar: confidence']
        panels=root/'panels';panels.mkdir(exist_ok=True);records=[]
        digest=hashlib.sha256((root/'index.html').read_bytes()).hexdigest()
        for i,mesh in enumerate(('frog','sculpt')):
            scale=1 if mesh=='frog' else 2
            js(f'$("mesh").value="{i}";$("mesh").dispatchEvent(new Event("change"));$("curveScale").value="{scale}";$("curveScale").dispatchEvent(new Event("input"));camera.zoom=1.65;')
            for j,name in enumerate(names):
                js(f'$("rightMode").value=String(selectedMesh.runs.findIndex(r=>r.name==={json.dumps(name)}));$("rightMode").dispatchEvent(new Event("change"));$("boundary").checked={str(j<6).lower()};$("curves").checked={str(j<6).lower()};$("seeds").checked={str(j<6).lower()};renderAll();')
                inspection=js('viewerInspection()')
                if inspection['glErrors']!=[0,0]:
                    raise RuntimeError(inspection)
                image=js('$("rightCanvas").toDataURL("image/png")');file=f'{mesh}-{j}.png'
                (panels/file).write_bytes(base64.b64decode(image.split(',')[1]))
                records.append(dict(mesh=mesh,stage=name,file=file,html_sha256=digest,inspection=inspection))
        (panels/'record.json').write_text(json.dumps(records,indent=2)+'\n')
        print(f'Captured {len(records)} depth-tested panels')
    finally:
        ws.close()


if __name__=='__main__':
    main()

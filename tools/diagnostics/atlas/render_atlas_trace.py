#!/usr/bin/env python3
"""Compose figures from depth-tested viewer panels and their bound scalar ranges.

Capture panels with capture_atlas_trace.py before rendering; Matplotlib handles
the publication layout, while WebGL supplies correct surface-line occlusion.
"""
import argparse
import gzip
import json
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np


def draw(mesh, names, output, scalars=False):
    panels=output.parent/'panels'
    records=json.loads((panels/'record.json').read_text())
    fig,axes=plt.subplots(2,3,figsize=(15,9),facecolor='white')
    t=np.linspace(0,1,256)
    cmap=matplotlib.colors.ListedColormap(np.column_stack((.12+.83*t,.38+.25*np.sin(np.pi*t),.88-.77*t)))
    for ax,name in zip(axes.flat,names):
        stage=next(x for x in mesh['stages'] if x['name']==name)
        panel=next(x for x in records if x['mesh']==mesh['name'] and x['stage']==name)
        ax.imshow(plt.imread(panels/panel['file']));ax.set_axis_off()
        if scalars:
            norm=matplotlib.colors.Normalize(*stage['range'])
            bar=fig.colorbar(matplotlib.cm.ScalarMappable(norm=norm,cmap=cmap),ax=ax,shrink=.6,pad=.01)
            bar.ax.tick_params(labelsize=8)
        title=name.replace('Scalar: ','').replace('feature: ','Earlier atlas: ').replace('protected: ','Protected atlas: ')
        title=title.replace('Final charts before native packing','final charts').replace('Initial 64 seeded clusters','initial seeds').replace('Validated charts before merging','before merging')
        ax.set_title(title+'\n'+(stage['units'] if scalars else f'{len(set(stage["labels"]))} clusters'),fontsize=10,wrap=True)
    fig.suptitle(mesh['name'].capitalize()+' · '+('Decision scalars' if scalars else 'Initial regions and atlas stages'),fontsize=18,y=.98)
    scale=1 if mesh['name']=='frog' else 2
    footer='Unlit scalar colors; display clipped to 2nd–98th percentiles except stated fixed limits. Gray = missing curve support.' if scalars else f'Depth-tested surface views · Black: cluster boundaries · Red/blue: principal ridges/valleys · Yellow: seeds\nCurve scale {scale} (0 = smallest), confidence ≥ 0.15. Curves are overlays; alignment is not an acceptance test.'
    fig.text(.5,.025,footer,ha='center',fontsize=10)
    fig.subplots_adjust(left=.015,right=.985,bottom=.08,top=.91,wspace=.05,hspace=.13)
    fig.savefig(output,dpi=135);plt.close(fig)


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('trace',type=Path);args=parser.parse_args()
    data=json.loads(gzip.decompress((args.trace/'stages.json.gz').read_bytes()))
    for mesh in data['meshes']:
        draw(mesh,['Native baseline: initial growth','Native baseline: final regions','feature: Initial 64 seeded clusters',
                   'feature: Final charts before native packing','protected: Validated charts before merging','protected: Final charts before native packing'],args.trace/f'{mesh["name"]}-stages.png')
        draw(mesh,['Scalar: signed k1','Scalar: signed k2','Scalar: native soft feature confidence',
                   'Scalar: max incident dihedral','protected: final local UV stretch','Unused curve scalar: confidence'],args.trace/f'{mesh["name"]}-scalars.png',True)


if __name__=='__main__':
    main()

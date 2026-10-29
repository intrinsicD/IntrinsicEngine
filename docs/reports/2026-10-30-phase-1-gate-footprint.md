# Phase 1 gate, size criterion (ADR 0031): kernel density and point spacing

Measured 2026-10-30 at `3a66c3efb` for `RUNTIME-351` (operator decision: stop building after block 1,
count the size, report). Build time and memory were not measured.

## Method
ADR 0031, Validation, Accounting: every line of an operation's own files and every operation-specific
line it adds to shared files, outside `src/geometry`; tests counted apart. A file counts when it names
the operation (its identifiers, its window, state, transaction and progress names) outside
`src/geometry` and documentation; the panel counts as its whole window function plus its other
lines naming the operation. One match in `Runtime.PointCloudConsolidationGpu.cpp` (that module's
own `Density` member) is unrelated and excluded below. The pre-pilot count at `f96a13e2d` is an
estimate: the operations' blocks inside shared files (codec section, method record, entry points,
window) are counted whole, the family template is framework and left out, and the session's sink
lines are undercounted. Scripts: `operation_footprint.py` and `operation_footprint_baseline.py`
below.

## Result

| | files touched | operation-specific lines | gate |
|---|---|---|---|
| kernel density, now | 13 | 477 | at most 9 files (3 own), 150 lines: **missed** |
| point spacing, now | 11 | 487 | **missed** |
| kernel density, pre-pilot (estimate) | 16 | about 391 | |
| point spacing, pre-pilot (estimate) | 14 | about 395 | |

Now, kernel density: own interface 160 and unit 173 lines, test; shared: panel 127, legacy
capability enum 5, agent row 4, section registration 2, build list 2, point field facade 1, module
inventory 1, tool schema baseline 2, `tests/CMakeLists.txt`. Point spacing: own 161 and 179, test;
panel 135, agent row 4, registration 2, build list 2, facade 1, inventory 1, schema baseline 2,
`tests/CMakeLists.txt`.

The framework modules (`Runtime.OperationRuntime.cppm` 518, `Runtime.OperationResults.cppm` 122,
`Runtime.DerivedConfigSections.cppm` 247 lines) hold 887 lines, about 690 of them new (the family
template they replace was 195 lines).

Reading: files per operation fell by 3; operation-specific lines rose by about 20%. The moved code is
clang-formatted now, each unit carries about 35 lines of module and include boilerplate, the
non-template entry points stay (ADR 0031 Decision 7), and the panel is unchanged. The two pilot
operations were the most factored before (one shared family template), so they could show the least
saving.

## Scripts

```python
# Static phase 1 size count (ADR 0031 Validation, Accounting) at the current HEAD.
import re, subprocess, pathlib
OPS={'kernel_density':dict(own=['src/runtime/Modules/KernelDensity/Runtime.KernelDensityConfig.cppm','src/runtime/Modules/KernelDensity/Runtime.KernelDensityOperation.cpp'],
        test='tests/contract/runtime/Test.KernelDensityOperations.cpp',window='DrawDensityWindow',
        pat=r'KernelDensity|kernel_density|DrawDensityWindow|DensityState|DensityTransaction|density_progress|\bDensity\.'),
     'point_spacing':dict(own=['src/runtime/Modules/PointSpacing/Runtime.PointSpacingConfig.cppm','src/runtime/Modules/PointSpacing/Runtime.PointSpacingOperation.cpp'],
        test='tests/contract/runtime/Test.PointSpacingOperations.cpp',window='DrawSpacingWindow',
        pat=r'PointSpacing|point_spacing|DrawSpacingWindow|SpacingState|SpacingTransaction|spacing_progress|\bSpacing\.')}
MPP='src/app/Sandbox/Editor/Sandbox.MeshProcessingPanels.cpp'
GENERATED={'docs/api/generated/module_inventory.md','tests/data/agent_declared_tool_schemas.json'}
def window_range(text, name):
    lines=text.split('\n')
    start=next(i for i,l in enumerate(lines) if f'void MeshProcessingPanels::Impl::{name}(' in l)
    depth=0; seen=False
    for i in range(start,len(lines)):
        depth+=lines[i].count('{')-lines[i].count('}')
        if '{' in lines[i]: seen=True
        if seen and depth==0: return start,i
for op,c in OPS.items():
    own=sum(len(pathlib.Path(f).read_text().splitlines()) for f in c['own'])
    files=subprocess.run(['grep','-rlE',c['pat'],'src','docs/api/generated','tests/data/agent_declared_tool_schemas.json'],capture_output=True,text=True).stdout.split()
    files=[f for f in files if not f.startswith('src/geometry/') and not f.endswith('.md') or f in GENERATED]
    shared={}
    for f in sorted(set(files)-set(c['own'])):
        text=pathlib.Path(f).read_text(); lines=text.split('\n')
        n=sum(1 for l in lines if re.search(c['pat'],l))
        if f==MPP:
            a,b=window_range(text,c['window'])
            inside=b-a+1
            outside=sum(1 for i,l in enumerate(lines) if (i<a or i>b) and re.search(c['pat'],l))
            # the registration block: RegisterWindow("view.<op>", ... ) spans several lines
            reg=[i for i,l in enumerate(lines) if f'RegisterWindow("view.{op}"' in l][0]
            j=reg
            while not lines[j].rstrip().endswith(');'): j+=1
            regblock=j-reg+1
            regnamed=sum(1 for i in range(reg,j+1) if re.search(c['pat'],lines[i]))
            n=inside+outside+regblock-regnamed
        shared[f]=n
    test=len(pathlib.Path(c['test']).read_text().splitlines())
    gen=sum(v for f,v in shared.items() if f in GENERATED)
    print(f'== {op}: own files {len(c["own"])} ({own} lines); shared files {len(shared)} ({sum(shared.values())} lines, of them generated {gen}); test file {test} lines')
    for f,v in shared.items(): print(f'   {v:4d}  {f}')
    print(f'   files touched (adding an operation): {len(c["own"])+1+len(shared)+1}  (own + test + shared + tests/CMakeLists.txt)')
    print(f'   operation-specific lines (own + shared, tests apart): {own+sum(shared.values())}')
```

```python
# Same accounting at the pre-pilot commit f96a13e2d (estimate: operation blocks inside shared files are
# counted whole; the family template PointFieldWork<M> is framework and left out).
import re, subprocess
REV='f96a13e2d'
def show(p): return subprocess.run(['git','show',f'{REV}:{p}'],capture_output=True,text=True).stdout
def block(text, start_marker, end_predicate):
    lines=text.split('\n'); a=next(i for i,l in enumerate(lines) if start_marker in l)
    b=end_predicate(lines,a); return b-a+1
def brace_end(lines,a):
    d=0; seen=False
    for i in range(a,len(lines)):
        d+=lines[i].count('{')-lines[i].count('}')
        if '{' in lines[i]: seen=True
        if seen and d==0: return i
for op,cfg,rec,win,pat in [('kernel_density','KernelDensity','KernelDensityMethod','DrawDensityWindow',r'KernelDensity|kernel_density|DrawDensityWindow|DensityState|DensityTransaction|density_progress|\bDensity\.'),
                          ('point_spacing','PointSpacing','PointSpacingMethod','DrawSpacingWindow',r'PointSpacing|point_spacing|DrawSpacingWindow|SpacingState|SpacingTransaction|spacing_progress|\bSpacing\.')]:
    files=subprocess.run(['git','grep','-lE',pat,REV,'--','src','docs/api/generated','tests/data/agent_declared_tool_schemas.json'],capture_output=True,text=True).stdout.split()
    files=[f.split(':',1)[1] for f in files]
    files=[f for f in files if not f.startswith('src/geometry/') and not f.endswith('README.md') and 'PointCloudConsolidationGpu' not in f]
    total=0; rows=[]
    for f in files:
        t=show(f); lines=t.split('\n')
        n=sum(1 for l in lines if re.search(pat,l))
        if f.endswith(f'Runtime.{cfg}Config.cppm'): n=len(lines)-1
        if f.endswith('Runtime.PointNeighborhoodConfigCodecs.cpp'):
            # rows block and the codec namespace block of this operation
            rows_a=next(i for i,l in enumerate(lines) if f'k{cfg}Fields[]' in l); rows_b=brace_end(lines,rows_a)
            cod_a=next(i for i,l in enumerate(lines) if f'{cfg}Config Parse{cfg}Config(' in l)
            ns_a=max(i for i in range(cod_a) if lines[i].startswith('namespace Extrinsic::Runtime'))
            ns_b=brace_end(lines,ns_a)
            n=(rows_b-rows_a+1)+(ns_b-ns_a+1)+2
        if f.endswith('Runtime.PointFieldOperations.Processing.cpp'):
            r_a=next(i for i,l in enumerate(lines) if f'struct {rec}' in l); r_b=brace_end(lines,r_a)
            n=(r_b-r_a+1)+sum(1 for i,l in enumerate(lines) if (i<r_a or i>r_b) and re.search(pat,l) and 'template' not in l)
        if f.endswith('Sandbox.MeshProcessingPanels.cpp'):
            w_a=next(i for i,l in enumerate(lines) if f'void MeshProcessingPanels::Impl::{win}(' in l); w_b=brace_end(lines,w_a)
            n=(w_b-w_a+1)+sum(1 for i,l in enumerate(lines) if (i<w_a or i>w_b) and re.search(pat,l))
        rows.append((n,f)); total+=n
    print(f'== {op} at {REV}: files {len(files)} (+ test, + tests/CMakeLists.txt) = {len(files)+2}; operation-specific lines {total}')
    for n,f in sorted(rows,reverse=True): print(f'   {n:4d}  {f}')
```

import hashlib,json,subprocess,statistics
from pathlib import Path
p=Path('ara/evidence/diagnostics/build011_point_config_codecs');raw=Path('/tmp/intrinsic-build011-measurement');protocol=json.loads((p/'protocol.json').read_text());identity=json.loads((p/'source-identity.json').read_text());summary=json.loads((p/'summary.json').read_text());h=lambda b:hashlib.sha256(b).hexdigest()
results=sorted([json.loads(x.read_text()) for x in (p/'results').glob('*.json')],key=lambda r:r['diagnostics']['position']);assert len(results)==4
expected_command=['cmake','--build','/dev/shm/intrinsic-build011-measure','--target','IntrinsicRuntimeContractTests','--parallel','4'];starts=[];dependencies=[];sums=[]
for r,d in zip(results,sorted(raw.glob('[0-9]*-*'))):
 arm=r['diagnostics']['arm'];assert r['source']=={'revision':identity[arm],'state':'clean_commit'};assert r['claim_eligible'] is False
 assert r['manifest']['sha256']==protocol['manifest_sha256']==h(Path(r['manifest']['path']).read_bytes());assert r['resolved_params']['jobs']==4
 assert r['diagnostics']['toolchain_backend']=='/usr/bin/clang++-23';assert r['resolved_params']['source_revisions']==protocol['manifest']['params']['source_revisions']
 for n,v in r['diagnostics']['scenarios'].items():
  assert v['execution']['command']==expected_command,(d,n)
  assert v['execution']['exit_code']==0
  assert abs(v['execution']['wall_ms']-r['metrics']['build_time_ms'][n])<1e-6
 starts.append(r['diagnostics']['scenarios']['clean']['execution']['started_at']);dependencies.append(json.loads((d/'dependencies.json').read_text()))
 rows=json.loads((d/'clean.hotspots.json').read_text());assert len(rows)==r['diagnostics']['scenarios']['clean']['compiler_invocations'];sums.append(sum(x['duration_ms'] for x in rows)/1000)
 for check in ['CMAKE_CXX_COMPILER_LAUNCHER:UNINITIALIZED=','CMAKE_C_COMPILER_LAUNCHER:UNINITIALIZED=']:
  assert check in (d/'CMakeCache.txt').read_text(),check
assert starts==sorted(starts);assert len({r['config_digest'] for r in results})==1
assert all(d==dependencies[0] for d in dependencies)
for f,arms in identity['source_hashes'].items():
 for arm,digest in arms.items():
  if digest is not None:assert h(subprocess.check_output(['git','show',identity[arm]+':'+f]))==digest,(f,arm)
for f,digest in identity['same_fixture_bytes_in_both_arms'].items():
 for arm in ['before','after']:assert h(subprocess.check_output(['git','show',identity[arm]+':'+f]))==digest
assert h(Path('tools/analysis/benchmark_compile_iteration.py').read_bytes())==protocol['runner_sha256']
for arm in ['before','after']:
 observed=[v for v,r in zip(sums,results) if r['diagnostics']['arm']==arm]
 assert observed==summary['clean_compiler_duration_sum_s'][arm]
(p/'integrity-audit.json').write_text(json.dumps({'all_four_sample_identities_commands_timings_and_flags_verified':True,'all_source_hashes_verified_against_git':True,'same_dependency_fingerprint_all_four':dependencies[0],'chronological_clean_starts':starts,'compiler_duration_sums_from_raw_hotspots_abba':sums,'canonical_result_count':4,'manifest_and_runner_hashes_verified':True},indent=2)+'\n')
print('All four samples: source, dependencies, manifest/runner hashes, commands, timing and compiler sums verified.')

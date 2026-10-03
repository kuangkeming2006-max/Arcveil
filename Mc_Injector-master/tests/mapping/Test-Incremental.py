"""Incremental state regressions; run Test-Resolver.py first to create fixtures."""
import json,sys,subprocess,copy
from pathlib import Path
b=Path(sys.argv[1]);f=b/'resolver-fixtures';exe=b/'MappingAnalyzer.exe'
c=Path(__file__).resolve().parents[2]/'mapping/contracts-v1.json'
def write(name,value):
 p=f/name;p.write_text(json.dumps(value),encoding='utf-8');return p
def read(name):
 lines=(f/name).read_text(encoding='utf-8').splitlines()
 if 'snapshotStreamVersion' not in lines[0]:return json.loads('\n'.join(lines))
 snapshot={};classes=[];data=''
 for line in lines[1:]:
  frame=json.loads(line);data+=frame['data']
  if frame['last']:
   item=json.loads(data);data=''
   if frame['kind']=='class':classes.append(item)
   else:snapshot.update(item)
 snapshot['classes']=classes;return snapshot
def run(args,code=0):
 r=subprocess.run([str(exe),*map(str,args)],capture_output=True,text=True,encoding='utf-8')
 assert r.returncode==code,(r.returncode,r.stdout,r.stderr)
 return [json.loads(x) for x in r.stdout.splitlines() if x]
raw=read('renamed-raw.json');raw['loaders'][0]['instance']=123
run(['inspect','--snapshot',write('incremental-raw.json',raw),'--out',f/'incremental-target.json'])
base=['resolve','--incremental','--pack',f/'pack.json','--reference',f/'reference.json','--contracts',c]
run(base+['--snapshot',f/'incremental-target.json','--out',f/'incremental-a.json'])
a=read('incremental-a.json');assert a['requiredComplete'] and len(a['accepted'])==257
assert all(v['provisional'] and v['confidence']==0.99 and v['bindingProof'] for v in a['accepted'].values())
e=run(base+['--snapshot',f/'incremental-target.json','--state',f/'incremental-a.json','--out',f/'incremental-b.json'])
assert sum(x['event']=='symbol-revalidated' for x in e)==257
assert not any(x['event']=='symbol-started' for x in e)
# Enumeration order changes leave normalized retention evidence unchanged.
reordered=copy.deepcopy(raw)
for cl in reordered['classes']:
 cl['methods'].reverse();cl['fields'].reverse()
run(['inspect','--snapshot',write('incremental-reordered-raw.json',reordered),'--out',f/'incremental-reordered.json'])
e=run(base+['--snapshot',f/'incremental-reordered.json','--state',f/'incremental-a.json','--out',f/'incremental-reordered-state.json'])
assert sum(x['event']=='symbol-revalidated' for x in e)==257
assert not any(x['event']=='symbol-started' for x in e)
# Same metadata but changed installed body invalidates evidence; lost unique correspondence
# cannot be retained just because its old runtime name still exists.
changed=copy.deepcopy(raw)
owner=next(x for x in changed['classes'] if x['methods'])
method=next(x for x in owner['methods'] if x.get('bytecode'))
method['bytecode']='00'+method['bytecode']
run(['inspect','--snapshot',write('incremental-change-raw.json',changed),'--out',f/'incremental-change.json'])
e=run(base+['--snapshot',f/'incremental-change.json','--state',f/'incremental-a.json','--out',f/'incremental-c.json'],4)
assert any(x['event']=='symbol-invalidated' for x in e)
assert any(x['event']=='symbol-revalidated' for x in e)
assert not read('incremental-c.json')['requiredComplete']
# State cannot cross process identity, reference or contract boundaries.
stale=copy.deepcopy(a);stale['processStart']='another-process'
e=run(base+['--snapshot',f/'incremental-target.json','--state',write('incremental-stale.json',stale),'--out',f/'incremental-d.json'])
assert sum(x['event']=='symbol-started' for x in e)==257
assert not any(x['event']=='symbol-revalidated' for x in e)
# Relevant-set hashes ignore unrelated class additions in the same runtime loader.
lite=copy.deepcopy(raw);lite['detailLevel']='lite';lite['captureKind']='jvmti-metadata-double-read'
for cl in lite['classes']:
 for key in ('constantPool','constantPoolCount','major','minor'):cl.pop(key,None)
 for method in cl['methods']:method.pop('bytecode',None)
run(['inspect','--lite','--snapshot',write('incremental-lite-raw.json',lite),'--out',f/'incremental-lite.json'])
select=['select','--allow-empty','--pack',f/'pack.json','--reference',f/'reference.json','--contracts',c]
run(select+['--snapshot',f/'incremental-lite.json','--out',f/'incremental-select-a.json'])
unrelated=copy.deepcopy(lite['classes'][0]);unrelated['name']='LUnrelatedWatchFixture;';unrelated['fields']=[];unrelated['methods']=[]
lite['classes'].append(unrelated)
run(['inspect','--lite','--snapshot',write('incremental-lite-b-raw.json',lite),'--out',f/'incremental-lite-b.json'])
run(select+['--snapshot',f/'incremental-lite-b.json','--out',f/'incremental-select-b.json'])
assert read('incremental-lite.json')['fingerprint']!=read('incremental-lite-b.json')['fingerprint']
assert read('incremental-select-a.json')['relevantFingerprint']==read('incremental-select-b.json')['relevantFingerprint']
# An incremental candidate never exports an authoritative pack.
sentinel=f/'incremental-sentinel.json';sentinel.write_text('keep',encoding='utf-8')
run(base+['--snapshot',f/'incremental-target.json','--out',f/'incremental-e.json','--write-pack',sentinel],1)
assert sentinel.read_text(encoding='utf-8')=='keep'
print('Incremental Analyzer: retention, affected invalidation, process binding, relevant scope and provisional export gate passed')

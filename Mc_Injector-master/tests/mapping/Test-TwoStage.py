"""Two-stage scope/loader regression fixtures; run Test-Resolver.py first."""
import copy, json, subprocess, sys
from pathlib import Path
b=Path(sys.argv[1]); f=b/'resolver-fixtures'; exe=str(b/'MappingAnalyzer.exe')
def run(args, ok=True):
 r=subprocess.run([exe,*map(str,args)],capture_output=True,encoding='utf-8')
 assert (r.returncode==0)==ok,(args,r.returncode,r.stdout[-2000:],r.stderr)
 return r

def write(name,value):
 path=f/name; path.write_text(json.dumps(value),encoding='utf-8'); return path

def read(path):
 lines=path.read_text(encoding='utf-8').splitlines()
 if 'snapshotStreamVersion' not in lines[0]:return json.loads('\n'.join(lines))
 snapshot={}; classes=[]; data=''
 for line in lines[1:]:
  frame=json.loads(line);data+=frame['data']
  if frame['last']:
   item=json.loads(data);data=''
   if frame['kind']=='class':classes.append(item)
   else:snapshot.update(item)
 snapshot['classes']=classes;return snapshot

raw=json.loads((f/'raw.json').read_text(encoding='utf-8'))
raw['detailLevel']='lite';raw['captureKind']='jvmti-metadata-double-read';raw['loaders'][0]['instance']=123
for c in raw['classes']:
 for key in ('constantPool','constantPoolCount','major','minor'):c.pop(key,None)
 for m in c['methods']:m.pop('bytecode',None)
unrelated=copy.deepcopy(raw['classes'][0]);unrelated['name']='LUnrelatedFixture;';unrelated['fields']=[];unrelated['methods']=[]
raw['classes'].append(unrelated)
source=write('lite-raw.json',raw);lite=f/'lite.jsonl'
run(['inspect','--snapshot',source,'--lite','--out',lite])
index=read(lite); assert index['loaderInstances'][0]['instance']==123
selection=f/'selection.json'
run(['select','--pack',f/'pack.json','--snapshot',lite,'--out',selection])
chosen=read(selection)
assert chosen['classes'] and all(c['name']!='LUnrelatedFixture;' for c in chosen['classes'])
assert all(c['metadataDigest'] and c['evidence'] for c in chosen['classes'])
assert chosen['liteFingerprint']==index['fingerprint'] and chosen['pid']==raw['pid']
for command in ('validate','resolve'):
 r=run([command,'--pack',f/'pack.json','--snapshot',lite,'--out',f/'forbidden.json'],False)
 assert 'inspect-detail' in r.stdout
# Loader enumeration order is not identity. Distinct instances with identical
# content must stay separate; identity-hash collisions cannot merge scopes.
renumbered=copy.deepcopy(raw);renumbered['loaders'][0]['id']=99
for c in renumbered['classes']:c['loader']=99
run(['inspect','--snapshot',write('renumbered-lite.json',renumbered),'--lite','--out',f/'renumbered.jsonl'])
assert read(f/'renumbered.jsonl')['fingerprint']==index['fingerprint']
duplicate=copy.deepcopy(raw);duplicate['loaders'].append({'id':2,'type':raw['loaders'][0]['type'],'instance':456})
for c in raw['classes']:
 c=copy.deepcopy(c);c['loader']=2;duplicate['classes'].append(c)
run(['inspect','--snapshot',write('duplicate-loader.json',duplicate),'--lite','--out',f/'duplicate-loader.jsonl'])
assert len({c['loaderKey'] for c in read(f/'duplicate-loader.jsonl')['classes']})==2
duplicate['loaders'][1]['instance']=123
r=run(['inspect','--snapshot',write('loader-collision.json',duplicate),'--lite'],False)
assert 'collision' in r.stdout
# Renamed candidates are selected structurally from the verified reference,
# without promoting metadata-only matches into accepted mappings.
renamed=json.loads((f/'renamed-raw.json').read_text(encoding='utf-8'))
renamed['detailLevel']='lite';renamed['captureKind']='jvmti-metadata-double-read';renamed['loaders'][0]['instance']=123
for c in renamed['classes']:
 for key in ('constantPool','constantPoolCount','major','minor'):c.pop(key,None)
 for m in c['methods']:m.pop('bytecode',None)
run(['inspect','--snapshot',write('renamed-lite-raw.json',renamed),'--lite','--out',f/'renamed-lite.jsonl'])
run(['select','--pack',f/'pack.json','--snapshot',f/'renamed-lite.jsonl','--out',f/'no-reference-selection.json'],False)
run(['select','--pack',f/'pack.json','--snapshot',f/'renamed-lite.jsonl','--reference',f/'reference.json','--out',f/'structural-selection.json'])
assert read(f/'structural-selection.json')['classes']
print('Two-stage fixtures: scoped candidates, structural reference, lite guard, stable loader IDs, distinct loader scopes and collision rejection passed')

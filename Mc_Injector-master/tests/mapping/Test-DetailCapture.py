import json,subprocess,sys
from pathlib import Path
b=Path(sys.argv[1]);pid=sys.argv[2];jdk=Path(sys.argv[3]);f=b/'mapping-fixture'
def read(path):
 snapshot={};classes=[];text=''
 for line in path.read_text(encoding='utf-8').splitlines()[1:]:
  frame=json.loads(line);text+=frame['data']
  if frame['last']:
   item=json.loads(text);text=''
   if frame['kind']=='class':classes.append(item)
   else:snapshot.update(item)
 snapshot['classes']=classes;return snapshot
snapshot=read(f/'snapshot.json');lite=read(f/'snapshot.json.lite.jsonl')
assert snapshot['detailLevel']=='selected' and len(snapshot['classes'])==1
subject=snapshot['classes'][0];assert subject['name']=='LMappingCaptureSubject;'
assert 'mapping-modified'.encode().hex() in subject['constantPool']
assert 'crossReferences' in subject
assert lite['stats']['bytecodeBytes']==0 and lite['stats']['constantPoolBytes']==0
selection=json.loads((f/'snapshot.json.candidates.json').read_text(encoding='utf-8'))
selection['classes'][0]['metadataDigest']='0'*64
bad=f/'stale-selection.json';bad.write_text(json.dumps(selection),encoding='utf-8')
base=[str(b/'MappingAnalyzer.exe'),'inspect-detail','--pid',pid,'--java',str(jdk/'bin/java.exe'),'--helper',str(b/'attach-helper/McOverlayAttachHelper.jar'),'--probe',str(b/'MappingProbe-v2.dll'),'--native-loader',str(b/'McOverlayNativeLoader.exe'),'--candidates',str(bad),'--out',str(f/'rejected-detail.jsonl')]
r=subprocess.run(base,capture_output=True,encoding='utf-8');assert r.returncode!=0 and 'metadata changed' in r.stdout,r.stdout
selection['processStart']='stale-process';bad.write_text(json.dumps(selection),encoding='utf-8')
r=subprocess.run(base,capture_output=True,encoding='utf-8');assert r.returncode!=0 and 'different JVM instance' in r.stdout,r.stdout
base[base.index('--candidates')+1]=str(f/'snapshot.json.candidates.json')
base[base.index('--out')+1]=str(f/'nonexistent-output-directory/result.jsonl')
r=subprocess.run(base,capture_output=True,encoding='utf-8');assert r.returncode!=0,r.stdout
records=[json.loads(line) for line in r.stdout.splitlines()]
paths=[item for item in records if item['event']=='CAPTURE_PATH'][-1]
assert paths['fallback']['captureFailure']['stage']=='output-file-open',paths
assert paths['fallback']['captureFailure']['win32Error']==3,paths
assert len([item for item in records if item['event']=='SNAPSHOT_STATS'])==2
print('Live two-stage capture: one selected transformed class, cross references, zero lite bytecode, metadata drift and stale-process rejection passed')

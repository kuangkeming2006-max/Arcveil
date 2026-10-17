"""Production Probe/Analyzer/cache-proof checks against a private Java 17 synthetic JVM.
No real Minecraft process, installed-client compatibility claim, or production cache writes.
Usage: Test-ModernPipeline.py BUILD JAVA FIXTURE
"""
import copy, json, subprocess, sys
from pathlib import Path
root=Path(__file__).resolve().parents[2]
build,java,fixture=map(lambda p:Path(p).resolve(),sys.argv[1:4])
folder=build/'modern-pipeline-fixtures';folder.mkdir(exist_ok=True)
pack=root/'mapping/packs/vanilla-1.20.1.json'
contracts=root/'mapping/contracts-v2.json'
analyzer=build/'MappingAnalyzer.exe'
checks=0
def run(args,code=0):
    global checks
    p=subprocess.run([str(analyzer),*map(str,args)],capture_output=True,text=True,timeout=90)
    assert p.returncode==code,(args,p.returncode,p.stdout[-3000:],p.stderr)
    checks+=1
    return p
def write(name,value):
    path=folder/name;path.write_text(json.dumps(value));return path
def read(path):return json.loads(path.read_text())
schema=folder/'schema.json'
run(['validate','--pack',pack,'--out',schema])
assert read(schema)['apiSupported'] and not read(schema)['injectionReady'];checks+=1
for field,value in [('playerSignature','Lwrong/Player;'),('getBounds',''),('mappingNamespace','Guess')]:
    wrong=copy.deepcopy(read(pack))
    d=wrong['providers'][0]['dictionaries'][0]
    if field=='mappingNamespace':d[field]=value
    else:d['symbols'][field]=value
    run(['validate','--pack',write('malformed-'+field+'.json',wrong),'--out',folder/'bad.json'],1)
for version in ('1.20.2','1.20.4','1.20.5','1.20.6'):
    wrong=copy.deepcopy(read(pack));wrong['gameVersion']=version
    result=run(['validate','--pack',write('unsupported-'+version+'.json',wrong),'--out',folder/'bad.json'],1)
    assert 'unsupported Minecraft API version' in result.stdout;checks+=1
process=subprocess.Popen([str(java),'-Xcheck:jni','-cp',str(fixture),'FixtureBoot'],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
try:
    assert process.stdout.readline().strip()=='FIXTURE_READY'
    snapshot=folder/'installed.jsonl'
    installed_capture=run(['inspect','--pid',process.pid,'--java',java,'--helper',build/'tools/McOverlayAttachHelper.jar',
         '--probe',build/'tools/MappingProbe-v16.dll','--native-loader',build/'tools/McOverlayNativeLoader.exe',
         '--pack',pack,'--contracts',contracts,'--out',snapshot])
    requests=[json.loads(line) for line in installed_capture.stdout.splitlines()
              if json.loads(line).get('event')=='CAPTURE_REQUEST']
    assert len(requests)==2 and all(event['runtimeAnchorCount']>0 for event in requests);checks+=1
    # Internal selected-detail capture has no --pack. It must retain independently
    # observed runtime identity without adding those fixed anchors to its scope.
    assert requests[-1]['runtimeAnchorCount']==4;checks+=1
    captured,selected,chunks=set(),set(),{}
    for line in snapshot.read_text().splitlines()[1:]:
        frame=json.loads(line);key=(frame['kind'],frame['index'])
        chunks[key]=chunks.get(key,'')+frame['data']
        if frame['last']:
            value=json.loads(chunks.pop(key))
            if frame['kind']=='class':captured.add(value['name'])
            elif 'captureScope' in value:selected={item['name'] for item in value['captureScope']['classes']}
    assert captured and captured==selected;checks+=1
    validation=folder/'validation.json'
    run(['validate','--pack',pack,'--contracts',contracts,'--snapshot',snapshot,'--out',validation])
    receipt=read(validation)
    assert receipt['injectionReady'] and receipt['identity']['bindingIdentity'] and receipt['identity']['runtimeBinding']['instance'];checks+=1
    assert receipt['identity']['versionVerified'] and receipt['identity']['minecraftVersion']=='1.20.1';checks+=1
    assert receipt['identity']['family']=='Vanilla';checks+=1
    # Positively identified Vanilla uses exact authored metadata without installed
    # bytecode capture, structural resolution, or structural-cache authorization.
    metadata=folder/'authored-metadata.jsonl'
    capture=run(['inspect','--lite','--required-only','--identity-lite','--pid',process.pid,
         '--java',java,'--helper',build/'tools/McOverlayAttachHelper.jar','--probe',build/'tools/MappingProbe-v16.dll',
         '--native-loader',build/'tools/McOverlayNativeLoader.exe','--pack',pack,'--contracts',contracts,'--out',metadata])
    stats=[json.loads(line) for line in capture.stdout.splitlines() if json.loads(line).get('event')=='SNAPSHOT_STATS']
    assert stats and all(event['jvmtiBytecodeBytes']==0 and event['jvmtiConstantPoolBytes']==0 for event in stats);checks+=1
    run(['validate-authored','--pack',pack,'--snapshot',metadata,'--contracts',contracts,'--out',folder/'authored.json'])
    authored=read(folder/'authored.json')
    assert authored['valid'] and authored['injectionReady'] and authored['validationMode']=='authored-metadata';checks+=1
    assert authored['identity']['versionVerified'] and authored['identity']['family']=='Vanilla';checks+=1
    assert authored['identity']['bindingIdentity']=='';checks+=1
    # A cache proof never replaces a fresh compact capture from this JVM.
    compact=folder/'compact.jsonl'
    run(['inspect','--lite','--binding-check','--required-only','--identity-lite','--pid',process.pid,
         '--java',java,'--helper',build/'tools/McOverlayAttachHelper.jar','--probe',build/'tools/MappingProbe-v16.dll',
         '--native-loader',build/'tools/McOverlayNativeLoader.exe','--pack',pack,'--contracts',contracts,'--out',compact])
    run(['validate-cache','--pack',pack,'--snapshot',compact,'--contracts',contracts,
         '--binding-identity',receipt['identity']['bindingIdentity'],'--out',folder/'cached.json'])
    run(['validate-cache','--pack',pack,'--snapshot',compact,'--contracts',contracts,
         '--binding-identity','0'*64,'--out',folder/'bad-cache.json'],3)
    # A valid installed-content Cache proof cannot supply missing runtime version authority.
    # Convert the captured synthetic fixture back to raw transport data and let
    # production inspect generate a new valid fingerprint; never forge that hash.
    offline_data,offline_classes,chunks={},[],{}
    for line in compact.read_text().splitlines()[1:]:
        frame=json.loads(line);key=(frame['kind'],frame['index'])
        chunks[key]=chunks.get(key,'')+frame['data']
        if frame['last']:
            value=json.loads(chunks.pop(key))
            if frame['kind']=='class':offline_classes.append(value)
            else:offline_data.update(value)
    ids={'bootstrap':0};loaders=[]
    for index,loader in enumerate(offline_data.pop('loaderInstances'),1):
        ids[loader['loaderKey']]=index
        loaders.append({'id':index,'type':loader['type'],'instance':loader['instance']})
    for klass in offline_classes:
        for ref in [klass,klass['super'],*klass['interfaces']]:
            ref['loader']=ids[ref.pop('loaderKey')]
    for key in ('normalizedVersion','fingerprint','runtimeEvidence'):offline_data.pop(key,None)
    offline_data.update(loaders=loaders,classes=offline_classes)
    offline_compact=folder/'no-runtime-version.jsonl'
    run(['inspect','--snapshot',write('no-runtime-version-raw.json',offline_data),'--out',offline_compact])
    run(['validate-cache','--pack',pack,'--snapshot',offline_compact,'--contracts',contracts,
         '--binding-identity',receipt['identity']['bindingIdentity'],'--out',folder/'no-version-cache.json'],3)
    assert read(folder/'no-version-cache.json')['reason']=='cached-runtime-version-evidence-unavailable-or-incompatible';checks+=1
    wrong=copy.deepcopy(read(pack));s=wrong['providers'][0]['dictionaries'][0]['symbols'];s['getBounds']=s['getEntityId']
    run(['validate','--pack',write('wrong-return-descriptor.json',wrong),'--snapshot',snapshot,
         '--contracts',contracts,'--out',folder/'bad-binding.json'],3)
    assert not read(folder/'bad-binding.json')['injectionReady'];checks+=1
    run(['validate','--pack',pack,'--snapshot',snapshot,'--contracts',root/'mapping/contracts-v1.json','--out',folder/'legacy-contract.json'],1)
    catalog=write('catalog.json',{'packs':[str(pack)]})
    run(['identify','--pack',root/'mapping/packs/default-v2.json','--snapshot',snapshot,
         '--contracts',contracts,'--identity-packs',catalog,'--out',folder/'identity.json'])
    assert read(folder/'identity.json')['minecraftVersion']=='1.20.1';checks+=1
    assert read(folder/'identity.json')['versionVerified'] and read(folder/'identity.json')['family']=='Vanilla';checks+=1
    # Family evidence must not be lost merely because only Vanilla ships in this pack.
    run(['inspect','--snapshot',snapshot,'--out',folder/'offline-family.json'])
    records=[json.loads(line) for line in (folder/'offline-family.json').read_text().splitlines()]
    header=next(record for record in records if record.get('kind')=='header')
    forge=json.loads(header['data']);forge['launchEvidence']=[{'family':'Forge','confidence':250}]
    header['data']=json.dumps(forge)
    forge_path=folder/'forge-evidence.jsonl'
    forge_path.write_bytes(('\n'.join(json.dumps(record,separators=(',',':')) for record in records)+'\n').encode())
    run(['identify','--pack',root/'mapping/packs/default-v2.json','--snapshot',forge_path,
         '--contracts',contracts,'--identity-packs',catalog,'--out',folder/'forge-identity.json'])
    forge_identity=read(folder/'forge-identity.json')
    assert forge_identity['versionVerified'] and forge_identity['minecraftVersion']=='1.20.1' and forge_identity['family']=='Forge';checks+=1
    run(['validate','--pack',pack,'--snapshot',forge_path,
         '--contracts',contracts,'--out',folder/'forge-rejected.json'],3)
    assert not read(folder/'forge-rejected.json')['injectionReady'];checks+=1
finally:
    process.terminate()
    try: stdout,stderr=process.communicate(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill();stdout,stderr=process.communicate()
    (folder/'synthetic-jvm-output.log').write_text(stdout+stderr)
print(f'{checks} modern Probe/Analyzer checks passed (real Java 17 JVM, synthetic Minecraft API; no game validation)')

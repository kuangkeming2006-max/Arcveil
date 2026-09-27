# Offline structural fixtures deliberately cover the complete logical schema.
# They are not real Minecraft classes and do not establish Lunar compatibility.
import json,sys,re,hashlib,subprocess,copy,struct
from pathlib import Path
build=Path(sys.argv[1]);root=Path(__file__).resolve().parents[2]
pack=json.loads((root/'mapping/packs/default-v1.json').read_text())
contract=json.loads((root/'mapping/contracts-v1.json').read_text())
p=copy.deepcopy(pack);p['providers']=[p['providers'][1]];d=p['providers'][0]['dictionaries'][0];sym=d['symbols']
classes={}
def sig(n):return 'L'+n.replace('.','/')+';'
def klass(n):
    if n not in classes:classes[n]={'name':n,'loader':1,'super':{'name':'Ljava/lang/Object;','loader':0},'interfaces':[],'fields':[],'methods':[],'modifiers':0,'major':52,'minor':0}
    return classes[n]
def desc(expr):return re.sub(r'\{(\w+)\}',lambda m:sym[m[1]],expr)
for key,spec in contract['symbols'].items():
    val=sym[key]
    if spec['kind']=='class' and val:klass(sig(val))
    if spec['kind']=='descriptor':
        for n in re.findall('L[^;]+;',val):
            if not n.startswith(('Ljava/','Ljavax/','Lcom/mojang/','Lorg/lwjgl/')):klass(n)
    if spec['kind'] not in ('method','field'):continue
    owner=sym[spec['owner']]
    if not owner:continue
    c=klass(sig(owner));values=val if isinstance(val,list) else [val]
    if spec.get('alternatives'):values=values[:1]
    for name in values:
        if not name:continue
        member={'name':name,'descriptor':desc(spec['descriptor']),'modifiers':8 if spec['static'] else 0}
        bucket=c['methods' if spec['kind']=='method' else 'fields']
        if member not in bucket:bucket.append(member)
for ctor in contract['constructors']:
    klass(sig(sym[ctor['owner']]))['methods'].append({'name':'<init>','descriptor':desc(ctor['descriptor']),'modifiers':0})
# Add a unique non-name constant to each class and method, plus all field usage edges.
for ci,(name,c) in enumerate(sorted(classes.items())):
    c['methods'].append({'name':'fixtureMarker','descriptor':'()V','modifiers':0})
    entries=[]
    def cp(tag,data):entries.append(bytes([tag])+data);return len(entries)
    def utf(t):b=t.encode();return cp(1,struct.pack('>H',len(b))+b)
    cls=cp(7,struct.pack('>H',utf(name[1:-1])))
    refs=[]
    for f in c['fields']:
        ni=cp(12,struct.pack('>HH',utf(f['name']),utf(f['descriptor'])))
        refs.append(cp(9,struct.pack('>HH',cls,ni)))
    for mi,m in enumerate(c['methods']):
        index=cp(3,struct.pack('>I',ci*1024+mi+1));code=bytes([0x13])+struct.pack('>H',index)+bytes([0x57])
        for f,ref in zip(c['fields'],refs):code+=bytes([0xb2 if f['modifiers']&8 else 0xb4])+struct.pack('>H',ref)+bytes([0x57])
        m['bytecode']=(code+bytes([0xb1])).hex()
    c['constantPool']=b''.join(entries).hex();c['constantPoolCount']=len(entries)+1
raw={'snapshotVersion':1,'complete':True,'captureKind':'jvmti-installed-double-read','pid':1,'processStart':'fixture','requestId':'fixture','loaders':[{'id':1,'type':'LFixtureLoader;'}],'classes':list(classes.values())}
folder=build/'resolver-fixtures';folder.mkdir(exist_ok=True)
def write(n,obj):(folder/n).write_text(json.dumps(obj,separators=(',',':')),encoding='utf-8')
write('pack.json',p);write('raw.json',raw)
exe=str(build/'MappingAnalyzer.exe')
def run(cmd,expected=0):
    result=subprocess.run([exe,*cmd],capture_output=True,text=True)
    if result.returncode!=expected:raise AssertionError((cmd,result.returncode,result.stdout[-1800:],result.stderr))
    return result
run(['inspect','--snapshot',str(folder/'raw.json'),'--out',str(folder/'reference.json')])
run(['validate','--pack',str(folder/'pack.json'),'--snapshot',str(folder/'reference.json'),'--out',str(folder/'validation.json')])
# Rename every application class and member, including CP symbolic references; retain structure.
renamed=copy.deepcopy(raw)
classmap={n:'Lrenamed/C'+str(i)+';' for i,n in enumerate(sorted(classes))}
membermap={}
for c in renamed['classes']:
    old=c['name'];c['name']=classmap[old]
    for bucket in ('fields','methods'):
        for i,m in enumerate(c[bucket]):
            if not m['name'].startswith('<'):membermap[(old,m['name'],m['descriptor'])]='f'+str(i) if bucket=='fields' else 'm'+str(i)
    pool=bytes.fromhex(c['constantPool']);pos=0;entries=[None]
    while pos<len(pool):
        tag=pool[pos];pos+=1
        if tag==1:
            length=int.from_bytes(pool[pos:pos+2],'big');pos+=2;text=pool[pos:pos+length].decode();pos+=length
            entries.append([tag,text])
        else:
            length={3:4,7:2,9:4,12:4}[tag];entries.append([tag,pool[pos:pos+length]]);pos+=length
    for tag,payload in entries[1:]:
        if tag==7:
            index=int.from_bytes(payload,'big');text=entries[index][1]
            if 'L'+text+';' in classmap:entries[index][1]=classmap['L'+text+';'][1:-1]
        elif tag==12:
            ni,di=struct.unpack('>HH',payload);n=entries[ni][1];ds=entries[di][1]
            entries[ni][1]=membermap.get((old,n,ds),n)
            for a,b in classmap.items():ds=ds.replace(a,b)
            entries[di][1]=ds
    encoded=[]
    for tag,payload in entries[1:]:
        if tag==1:b=payload.encode();encoded.append(bytes([tag])+struct.pack('>H',len(b))+b)
        else:encoded.append(bytes([tag])+payload)
    c['constantPool']=b''.join(encoded).hex()
    for bucket in ('fields','methods'):
        for m in c[bucket]:
            m['name']=membermap.get((old,m['name'],m['descriptor']),m['name'])
            for a,b in classmap.items():m['descriptor']=m['descriptor'].replace(a,b)
write('renamed-raw.json',renamed)
run(['inspect','--snapshot',str(folder/'renamed-raw.json'),'--out',str(folder/'target.json')])
run(['resolve','--pack',str(folder/'pack.json'),'--reference',str(folder/'reference.json'),'--snapshot',str(folder/'target.json'),'--out',str(folder/'candidate.json'),'--write-pack',str(folder/'resolved.json')])
candidate=json.loads((folder/'candidate.json').read_text());assert candidate['complete'] and len(candidate['symbols'])==257
assert all(v['confidence']>=v['threshold'] and v['evidence'] for v in candidate['symbols'])
run(['validate','--pack',str(folder/'resolved.json'),'--snapshot',str(folder/'target.json')])
# Missing reference cannot write a final pack; original output sentinel remains untouched.
(folder/'sentinel.json').write_text('do not replace')
run(['resolve','--pack',str(folder/'pack.json'),'--snapshot',str(folder/'target.json'),'--out',str(folder/'unresolved.json'),'--write-pack',str(folder/'sentinel.json')],4)
assert (folder/'sentinel.json').read_text()=='do not replace'
# Duplicate structural class is ambiguous even when one name is exactly the old name.
ambiguous=copy.deepcopy(renamed);duplicate=copy.deepcopy(ambiguous['classes'][0]);duplicate['name']='LDecoy;';ambiguous['classes'].append(duplicate)
write('ambiguous-raw.json',ambiguous)
run(['inspect','--snapshot',str(folder/'ambiguous-raw.json'),'--out',str(folder/'ambiguous.json')])
run(['resolve','--pack',str(folder/'pack.json'),'--reference',str(folder/'reference.json'),'--snapshot',str(folder/'ambiguous.json'),'--out',str(folder/'ambiguous-candidate.json')],4)
# A same-shape but incorrect optional object descriptor must fail final validation.
wrong=copy.deepcopy(renamed)
owner=classmap[sig(sym['gameSettingsName'])]
field=membermap[(sig(sym['gameSettingsName']),sym['keyBindSprintField'],sym['keyBindingSignature'])]
replacement=classmap[sym['entitySignature']]
c=next(c for c in wrong['classes'] if c['name']==owner)
next(f for f in c['fields'] if f['name']==field)['descriptor']=replacement
pool=bytes.fromhex(c['constantPool']);pos=0;entries=[None]
while pos<len(pool):
    tag=pool[pos];pos+=1
    if tag==1:
        length=int.from_bytes(pool[pos:pos+2],'big');pos+=2;text=pool[pos:pos+length].decode();pos+=length;entries.append([tag,text])
    else:
        length={3:4,7:2,9:4,12:4}[tag];entries.append([tag,pool[pos:pos+length]]);pos+=length
for tag,payload in entries[1:]:
    if tag==12:
        ni,di=struct.unpack('>HH',payload)
        if entries[ni][1]==field:entries[di][1]=replacement
encoded=[]
for tag,payload in entries[1:]:
    if tag==1:b=payload.encode();encoded.append(bytes([tag])+struct.pack('>H',len(b))+b)
    else:encoded.append(bytes([tag])+payload)
c['constantPool']=b''.join(encoded).hex()
write('wrong-descriptor-raw.json',wrong)
run(['inspect','--snapshot',str(folder/'wrong-descriptor-raw.json'),'--out',str(folder/'wrong-descriptor.json')])
run(['resolve','--pack',str(folder/'pack.json'),'--reference',str(folder/'reference.json'),'--snapshot',str(folder/'wrong-descriptor.json'),'--out',str(folder/'wrong-descriptor-candidate.json')],4)
print('Resolver: full-schema renamed classes/members, runtime validation, confidence/evidence, missing reference, ambiguity and incorrect optional descriptor fail-closed passed')

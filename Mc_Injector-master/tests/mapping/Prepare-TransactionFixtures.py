"""Run Test-Resolver.py first. Build family-scoped real Analyzer fixtures."""
import copy,json,struct,subprocess,sys
from pathlib import Path
b=Path(sys.argv[1]); f=b/'resolver-fixtures'; exe=str(b/'MappingAnalyzer.exe')
def write(name,data): (f/name).write_text(json.dumps(data),encoding='utf-8')
def run(*args):
    r=subprocess.run([exe,*map(str,args)],capture_output=True,text=True)
    assert r.returncode==0,(args,r.stdout[-2000:],r.stderr)
p=json.loads((f/'pack.json').read_text()); p['providers'][0]['family']='Lunar'
for d in p['providers'][0]['dictionaries']:
    d['family']='Lunar'; d['detection']=[{'match':2,'value':'lunar','confidence':250}]
write('lunar-pack.json',p)
raw=json.loads((f/'raw.json').read_text()); raw['launchEvidence']=[{'family':'Lunar','confidence':250}]; raw['loaders'][0]['instance']=123
write('lunar-raw.json',raw)
run('inspect','--snapshot',f/'lunar-raw.json','--out',f/'lunar-reference.json')
run('validate','--pack',f/'lunar-pack.json','--snapshot',f/'lunar-reference.json','--out',f/'lunar-validation.json')
def update(raw):
    raw=copy.deepcopy(raw);raw['launchEvidence']=[{'family':'Lunar','confidence':250}];raw['loaders'][0]['instance']=123
    for c in raw['classes']:
        pool=bytearray.fromhex(c['constantPool']);pos=0
        while pos<len(pool):
            tag=pool[pos];pos+=1
            if tag==1: n=int.from_bytes(pool[pos:pos+2],'big');pos+=2+n
            elif tag==3:
                value=int.from_bytes(pool[pos:pos+4],'big');pool[pos:pos+4]=(value+200000).to_bytes(4,'big');pos+=4
            else: pos+={7:2,9:4,12:4}[tag]
        c['constantPool']=pool.hex()
    return raw
write('updated-raw.json',update(json.loads((f/'renamed-raw.json').read_text())))
write('updated-reference-raw.json',update(raw)); write('updated-pack.json',p)
run('inspect','--snapshot',f/'updated-reference-raw.json','--out',f/'updated-reference.json')
run('validate','--pack',f/'updated-pack.json','--snapshot',f/'updated-reference.json','--out',f/'updated-validation.json')
print('Prepared Lunar source/renamed/installed-code-update fixtures with real validation')

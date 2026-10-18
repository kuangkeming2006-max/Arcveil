"""Run Test-Resolver.py first. Real Analyzer with synthetic JVM snapshots."""
import copy, json, subprocess, sys
from pathlib import Path
b=Path(sys.argv[1]); f=b/'resolver-fixtures'; exe=b/'MappingAnalyzer.exe'
def write(name,value):
    path=f/name;path.write_text(json.dumps(value),encoding='utf-8');return path
def run(args,code=0):
    result=subprocess.run([str(exe),*map(str,args)],capture_output=True,encoding='utf-8')
    assert result.returncode==code,(args,result.returncode,result.stdout[-2500:],result.stderr)
pack=json.loads((f/'pack.json').read_text())
# A descriptor-compatible 1.8 fixture does not authorize any other API version.
wrong=copy.deepcopy(pack);wrong['gameVersion']='1.12.2';path=write('unsupported-version-pack.json',wrong)
run(['validate','--pack',path,'--out',f/'version-schema.json'])
schema=json.loads((f/'version-schema.json').read_text())
assert schema['valid'] and not schema['injectionReady'] and not schema['apiSupported']
for command,extra,code in [('validate',[],3),('validate-cache',['--binding-identity','a'*64],3),
                          ('resolve',['--reference',f/'reference.json'],4),
                          ('resolve',['--incremental','--reference',f/'reference.json'],4)]:
    out=f/('unsupported-'+command+('-incremental' if '--incremental' in extra else '')+'.json')
    run([command,'--pack',path,'--snapshot',f/'reference.json','--out',out,*extra],code)
    result=json.loads(out.read_text())
    assert not result['injectionReady'] and result['unsupportedApi'] and result['minecraftVersion']=='1.12.2'
    assert 'Version Adapter' in result['reason']
# Schema-2 carries authored namespace, and recovered runtime names become Custom.
v2=copy.deepcopy(pack);v2['schemaVersion']=2
for provider in v2['providers']:
    for dictionary in provider['dictionaries']:dictionary['mappingNamespace']='Notch'
path=write('explicit-namespace-pack.json',v2)
run(['validate','--pack',path,'--snapshot',f/'reference.json','--out',f/'namespace-validation.json'])
run(['resolve','--pack',path,'--reference',f/'reference.json','--snapshot',f/'target.json','--out',f/'namespace-resolved.json'])
resolved=json.loads((f/'namespace-resolved.json').read_text())
assert resolved['complete'] and resolved['pack']['providers'][0]['dictionaries'][0]['mappingNamespace']=='Custom'
# Invalid required descriptor must still produce the logical key in diagnostics.
bad=json.loads((f/'raw.json').read_text())
symbols=pack['providers'][0]['dictionaries'][0]['symbols']
for klass in bad['classes']:
    if klass['name']=='L'+symbols['minecraftName'].replace('.','/')+';':
        for field in klass['fields']:
            if field['name']==symbols['playerField']:field['descriptor']='Ljava/lang/Object;'
run(['inspect','--snapshot',write('wrong-core-descriptor-raw.json',bad),'--out',f/'wrong-core-descriptor.json'])
run(['validate','--pack',f/'pack.json','--snapshot',f/'wrong-core-descriptor.json','--out',f/'wrong-core-validation.json'],3)
validation=json.loads((f/'wrong-core-validation.json').read_text())
assert any(row['symbol']=='playerField' and not row['accepted'] for attempt in validation['attempts'] for row in attempt['symbols'])
print('Version adapters: schema-only vs API support, full/cache/incremental refusal, namespace remap and required descriptor diagnostics passed (synthetic JVM snapshots)')

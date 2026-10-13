"""Generate a deliberately synthetic Java 17 API fixture from authored logical contracts.
Not a recovered Minecraft JAR, runtime mapping reference, or real game validation.
"""
import json, re, sys
from pathlib import Path
root=Path(__file__).resolve().parents[2]
out=Path(sys.argv[1]).resolve();out.mkdir(parents=True,exist_ok=True)
pack=json.loads((root/'mapping/packs/vanilla-1.20.1.json').read_text())
s=pack['providers'][0]['dictionaries'][0]['symbols']
contract=json.loads((root/'mapping/contracts-1.20.1.json').read_text())
def java_type(desc):
    if desc.startswith('L'):return desc[1:-1].replace('/','.')
    return {'V':'void','Z':'boolean','I':'int','F':'float','D':'double'}[desc]
def render(desc): return re.sub(r'\{(\w+)\}',lambda m:s[m[1]],desc)
sources={v:[] for k,v in s.items() if k.endswith('Name') and contract['symbols'][k]['kind']=='class'}
screen=java_type(s['guiScreenSignature']);sources[screen]=[]
for key,spec in contract['symbols'].items():
    if spec['kind'] not in ('field','method'):continue
    owner=s[spec['owner']];desc=render(spec['descriptor']);names=s[key] if isinstance(s[key],list) else [s[key]]
    for index,name in enumerate(names):
        if spec['kind']=='field':
            init=''
            if key=='previousPositionFields':init=' = '+str([10.,21.,32.][index])
            if key=='aabbFields':init=' = '+str([10.7,22.,32.7,11.3,23.8,33.3][index])
            sources[owner].append(f'public {java_type(desc)} {name}{init};')
        else:
            result=java_type(desc[2:]);body=''
            if key=='getMinecraft':body='return FixtureBoot.minecraft;'
            elif key=='getCurrentVersion':body=f'return new {s["worldVersionName"]}();'
            elif key=='getVersionName':body='return FixtureBoot.version;'
            elif key=='getLoadedEntities':body='return FixtureBoot.entities;'
            elif key=='getName':body=f'return new {s["chatComponentName"]}();'
            elif key=='getFormattedText':body='return "ModernFixture";'
            elif key=='getBounds':body=f'return new {s["aabbName"]}();'
            elif key=='getEntityId':body='return System.identityHashCode(this);'
            elif key=='positionMethods':body='return FixtureBoot.bad ? Double.NaN : '+str([11.,22.,33.][index])+';'
            elif key=='rotationMethods':body='return '+str([90,15][index])+'f;'
            elif key=='getHealth':body='return 17f;'
            elif key=='getMaxHealth':body='return 20f;'
            elif key=='getEyeHeight':body='return 1.62f;'
            elif key=='isInvisible':body='return false;'
            elif key=='isRemoved':body='return fixtureRemoved;'
            elif key=='isAlive':body='return !fixtureRemoved;'
            elif key=='isMouseGrabbed':body='return FixtureBoot.grabbed;'
            elif key=='grabMouse':body='FixtureBoot.grabbed=true;'
            elif key=='releaseMouse':body='FixtureBoot.grabbed=false;'
            elif result=='boolean':body='return true;'
            else:raise AssertionError((key,desc))
            sources[owner].append(f'public {"static " if spec["static"] else ""}{result} {name}() {{ {body} }}')
sources[s['entityName']].append('public boolean fixtureRemoved=false;')
parents={s['livingName']:s['entityName'],s['entityPlayerName']:s['livingName'],s['playerName']:s['entityPlayerName'],s['worldClientName']:s['worldName']}
for name,items in sources.items():
    assert '.' not in name, 'This fixture expects the exact pinned Vanilla obfuscated namespace'
    parent=' extends '+parents[name] if name in parents else ''
    (out/(name+'.java')).write_text('public class '+name+parent+' {\n'+'\n'.join(items)+'\n}\n')
boot=f'''public class FixtureBoot {{
 public static String version="1.20.1";
 public static boolean bad=false,grabbed=true;
 public static {s['minecraftName']} minecraft;
 public static java.util.List<{s['entityName']}> entities;
 public static void boot() {{
  version="1.20.1";bad=false;grabbed=true;
  minecraft=new {s['minecraftName']}();
  minecraft.{s['playerField']}=new {s['playerName']}();
  minecraft.{s['worldField']}=new {s['worldClientName']}();
  minecraft.{s['mouseHandlerField']}=new {s['mouseHandlerName']}();
  {s['entityPlayerName']} other=new {s['entityPlayerName']}();
  {s['livingName']} removed=new {s['livingName']}();
  removed.fixtureRemoved=true;
  entities=java.util.Arrays.asList(minecraft.{s['playerField']},other,removed);
 }}
 public static void wrongVersion() {{version="1.20.2";}}
 public static void badPosition() {{bad=true;}}
 public static void resetPosition() {{bad=false;}}
 public static void clearWorld() {{minecraft.{s['playerField']}=null;minecraft.{s['worldField']}=null;}}
 public static void main(String[] args) throws Exception {{boot();System.out.println("FIXTURE_READY");System.out.flush();Thread.sleep(120000);}}
}}
'''
(out/'FixtureBoot.java').write_text(boot)
brand=out/'net/minecraft/client/ClientBrandRetriever.java';brand.parent.mkdir(parents=True,exist_ok=True)
brand.write_text('''package net.minecraft.client;
public class ClientBrandRetriever {
 public static String getClientModName() {return System.getProperty("fixture.brand", "vanilla");}
}
''')
main=out/'net/minecraft/client/main/Main.java';main.parent.mkdir(parents=True,exist_ok=True)
main.write_text('package net.minecraft.client.main; public class Main {}\n')
# Runtime evidence is independent of pack-declared gameVersion. This resource is
# deliberately synthetic; production Probe reads it through the defining loader.
(out/'version.json').write_text(json.dumps({'id':'1.20.1'}))
(out/'sources.txt').write_text('\n'.join('"'+p.as_posix()+'"' for p in out.rglob('*.java')))
print('Synthetic modern fixture sources:',len(sources)+3)

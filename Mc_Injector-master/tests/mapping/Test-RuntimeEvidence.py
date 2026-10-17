"""Probe runtime-version/brand regression using owned synthetic Java 8/17 JVMs.

Usage: Test-RuntimeEvidence.py BUILD TARGET_JAVA JAVAC ATTACH_JAVA
This exercises installed JNI/JVMTI evidence, not real Minecraft compatibility.
"""
import json
import subprocess
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[2]
build, java, javac, attach_java = (Path(value).resolve() for value in sys.argv[1:5])
folder = build / ('runtime-evidence-' + java.parent.parent.name)
folder.mkdir(exist_ok=True)
checks = 0


def compile_sources(destination, files):
    destination.mkdir(exist_ok=True)
    sources = []
    for name, source in files.items():
        path = destination / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(source)
        sources.append(str(path))
    subprocess.run([str(javac), '--release', '8', '-d', str(destination), *sources], check=True)


brand = '''package net.minecraft.client; public class ClientBrandRetriever {
 public static String getClientModName() {return System.getProperty("fixture.brand", "vanilla");}
}'''
realms = '''package net.minecraft.realms; public class RealmsSharedConstants {
 public static String VERSION_STRING="1.8.9";
}'''
main = 'package net.minecraft.client.main; public class Main {}'
base = {'ave.java': 'public class ave {}', 'enn.java': 'public class enn {}',
        'net/minecraft/client/main/Main.java': main}
for name, version, with_brand, with_realms in [
        ('legacy', None, True, True), ('modern', '1.20.1', True, False),
        ('unknown', '1.20.1', False, False), ('conflict', '1.20.1', True, True),
        ('unsupported', '1.20.4', True, False), ('malformed', '', True, True), ('oversized', '', True, True)]:
    files = dict(base)
    if with_brand:
        files['net/minecraft/client/ClientBrandRetriever.java'] = brand
    if with_realms:
        files['net/minecraft/realms/RealmsSharedConstants.java'] = realms
    compile_sources(folder / name, files)
    if version:
        (folder / name / 'version.json').write_text(json.dumps({'id': version}))
(folder / 'malformed/version.json').write_text('{bad-json')
(folder / 'oversized/version.json').write_text(' ' * 65537)

compile_sources(folder / 'boot', {'EvidenceBoot.java': '''
import java.io.File; import java.net.URL; import java.net.URLClassLoader;
public class EvidenceBoot {
 private static final java.util.List<ClassLoader> loaders=new java.util.ArrayList<ClassLoader>();
 public static void main(String[] args) throws Exception {
  for (String path:args) {
   ClassLoader loader=new URLClassLoader(new URL[]{new File(path).toURI().toURL()}, EvidenceBoot.class.getClassLoader().getParent());
   loaders.add(loader);
   Class.forName("ave",true,loader); Class.forName("enn",true,loader);
   Class.forName("net.minecraft.client.main.Main",true,loader);
  }
  System.out.println("FIXTURE_READY");System.out.flush();Thread.sleep(120000);
 }
}''', 'DelegatingBoot.java': '''
import java.io.File; import java.net.URL; import java.net.URLClassLoader;
public class DelegatingBoot {
 private static ClassLoader parent, child;
 public static void main(String[] args) throws Exception {
  parent=new URLClassLoader(new URL[]{new File(args[0]).toURI().toURL()},DelegatingBoot.class.getClassLoader().getParent());
  child=new URLClassLoader(new URL[]{new File(args[1]).toURI().toURL()},parent) {
   protected Class<?> loadClass(String name,boolean resolve) throws ClassNotFoundException {
    if (!name.equals("ave")) return super.loadClass(name,resolve);
    Class<?> type=findLoadedClass(name); if(type==null)type=findClass(name);
    if(resolve)resolveClass(type); return type;
   }
  };
  Class.forName("ave",true,child);Class.forName("enn",true,parent);
  Class.forName("net.minecraft.client.main.Main",true,parent);
  System.out.println("FIXTURE_READY");System.out.flush();Thread.sleep(120000);
 }
}'''} )
compile_sources(folder / 'collider', {'ave.java':'public class ave {}'})


def snapshot_from_stream(path):
    snapshot, chunks = {}, {}
    for line in path.read_text().splitlines()[1:]:
        frame = json.loads(line)
        key = (frame['kind'], frame['index'])
        chunks[key] = chunks.get(key, '') + frame['data']
        if frame['last']:
            value = json.loads(chunks.pop(key))
            if frame['kind'] in ('header', 'footer'):
                snapshot.update(value)
    return snapshot


def capture(name, scopes, brand_value=None, boot='EvidenceBoot'):
    global checks
    command = [str(java), '-Xcheck:jni']
    if brand_value:
        command.append('-Dfixture.brand=' + brand_value)
    command += ['-cp', str(folder / 'boot'), boot, *map(str, scopes)]
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        assert process.stdout.readline().strip() == 'FIXTURE_READY'
        output = folder / (name + '.jsonl')
        result = subprocess.run([str(build / 'MappingAnalyzer.exe'), 'inspect', '--lite', '--identity-lite',
                                 '--required-only', '--detect-only', '--pid', str(process.pid),
                                 '--java', str(attach_java), '--helper', str(build / 'tools/McOverlayAttachHelper.jar'),
                                 '--probe', str(build / 'tools/MappingProbe-v16.dll'),
                                 '--native-loader', str(build / 'tools/McOverlayNativeLoader.exe'),
                                 '--pack', str(root / 'mapping/packs/default-v2.json'),
                                 '--contracts', str(root / 'mapping/contracts-v2.json'), '--out', str(output)],
                                capture_output=True, text=True, timeout=90)
        assert result.returncode == 0, (name, result.stdout, result.stderr)
        snapshot = snapshot_from_stream(output)
        evidence = snapshot['runtimeEvidence']
        assert evidence and all(item['loaderKey'] for item in evidence)
        assert all(item['anchor'] in ('Lave;', 'Lenn;', 'Lnet/minecraft/client/main/Main;') for item in evidence)
        assert not any(item['family'] == 'Vanilla' for item in snapshot['launchEvidence'])
        checks += 4
        return evidence
    finally:
        process.terminate()
        try:
            stdout, stderr = process.communicate(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()
            stdout, stderr = process.communicate()
        (folder / (name + '-jvm.log')).write_text(stdout + stderr)
        assert 'WARNING in native method' not in stderr and 'FATAL ERROR in native method' not in stderr, stderr


legacy = capture('legacy', [folder / 'legacy'])
assert {item['version'] for item in legacy} == {'1.8.9'}
assert {item['versionSource'] for item in legacy} == {'realms-shared-constants'}
assert {item['brand'] for item in legacy} == {'vanilla'}
checks += 3
modern = capture('modern-collision', [folder / 'modern'])
assert {item['version'] for item in modern} == {'1.20.1'}
assert {item['versionSource'] for item in modern} == {'loader-version-json'}
assert len({item['loaderKey'] for item in modern}) == 1
checks += 3
forge = capture('forge', [folder / 'modern'], 'forge')
assert {item['version'] for item in forge} == {'1.20.1'} and {item['brand'] for item in forge} == {'forge'}
checks += 1
legacy_forge = capture('legacy-forge-canonical-brand', [folder / 'legacy'], 'fml,forge')
assert {item['version'] for item in legacy_forge} == {'1.8.9'}
assert {item['brand'] for item in legacy_forge} == {'fml,forge'}
checks += 2
unrecognized_forge = capture('legacy-forge-extra-brand-token', [folder / 'legacy'], 'fml,forge,unknown')
assert {item['version'] for item in unrecognized_forge} == {'1.8.9'}
assert {item['brand'] for item in unrecognized_forge} == {'modified'}
checks += 2
unknown = capture('unknown-family', [folder / 'unknown'])
assert all(item['brand'] == '' for item in unknown)
checks += 1
conflict = capture('conflict', [folder / 'conflict'])
assert all(item['version'] == '' and item['diagnostic'] == 'conflicting runtime version evidence' for item in conflict)
checks += 1
unsupported = capture('unsupported', [folder / 'unsupported'])
assert {item['version'] for item in unsupported} == {'1.20.4'}
checks += 1
for name, diagnostic in [('malformed', 'malformed runtime version resource'),
                         ('oversized', 'runtime version resource unreadable or exceeds 65536 bytes')]:
    invalid = capture(name, [folder / name])
    assert all(item['version'] == '' and item['diagnostic'] == diagnostic for item in invalid)
    checks += 1
separate = capture('separate-loaders', [folder / 'legacy', folder / 'modern'])
versions = {}
for item in separate:
    versions.setdefault(item['loaderKey'], set()).add(item['version'])
assert len(versions) == 2 and set.union(*versions.values()) == {'1.8.9', '1.20.1'}
assert all(len(value) == 1 for value in versions.values())
checks += 2
delegated = capture('parent-resource-collision', [folder / 'modern', folder / 'collider'], boot='DelegatingBoot')
assert {item['version'] for item in delegated} == {'', '1.20.1'}
assert all(item['anchor']=='Lave;' and not item['brand'] and
           item['diagnostic']=='version resource has no independent game witness in anchor defining loader'
           for item in delegated if not item['version'])
checks += 2
print(f'{checks} runtime evidence checks passed; owned synthetic JVM: {java}')

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
        ('unsupported', '1.20.4', True, False), ('malformed', '', True, True), ('oversized', '', True, True),
        ('invalid-id', 'lunar-internal-version', True, True),
        ('malformed-no-legacy', '', True, False), ('oversized-no-legacy', '', True, False),
        ('invalid-id-no-legacy', 'lunar-internal-version', True, False)]:
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
(folder / 'malformed-no-legacy/version.json').write_text('{bad-json')
(folder / 'oversized-no-legacy/version.json').write_text(' ' * 65537)

# The constants holder is the audited class, not a guessed API binding name.
# Only the VERSION_STRING field is renamed. A pinned candidate pass must still
# prove the protocol/tick discriminators and a unique exact version value.
def semantic_holder(protocol=47, ticks=20, extra='', unreadable=False):
    version = ('readVersion()' if unreadable else '"1.8.9"')
    failure = ('private static String readVersion() {throw new IllegalStateException("fixture unreadable version");}'
               if unreadable else '')
    return f'''package net.minecraft.realms; public class RealmsSharedConstants {{
 public static int NETWORK_PROTOCOL_VERSION={protocol}, TICKS_PER_SECOND={ticks};
 private static String renamedVersion={version}; {extra} {failure}
}}'''


for name, holder in [
        ('renamed-legacy', semantic_holder()),
        ('renamed-wrong-protocol', semantic_holder(protocol=48)),
        ('renamed-wrong-ticks', semantic_holder(ticks=21)),
        ('renamed-extra-version', semantic_holder(extra='public static String otherVersion="1.20.1";')),
        ('renamed-too-many-fields', semantic_holder(extra=''.join(f'public static int filler{i};' for i in range(30)))),
        ('renamed-unreadable', semantic_holder(unreadable=True))]:
    files = dict(base)
    files['net/minecraft/client/ClientBrandRetriever.java'] = brand
    files['net/minecraft/realms/RealmsSharedConstants.java'] = holder
    compile_sources(folder / name, files)
    (folder / name / 'version.json').write_text(json.dumps({'id': 'lunar-internal-version'}))

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
}''', 'ErroneousHolderBoot.java': '''
import java.io.File; import java.net.URL; import java.net.URLClassLoader;
public class ErroneousHolderBoot {
 private static ClassLoader loader;
 public static void main(String[] args) throws Exception {
  loader=new URLClassLoader(new URL[]{new File(args[0]).toURI().toURL()},ErroneousHolderBoot.class.getClassLoader().getParent());
  try {Class.forName("net.minecraft.realms.RealmsSharedConstants",true,loader);}catch(Throwable expected){}
  Class.forName("ave",true,loader); Class.forName("enn",true,loader);
  Class.forName("net.minecraft.client.main.Main",true,loader);
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


def capture(name, scopes, brand_value=None, boot='EvidenceBoot', candidates=False, pin_anchor='Lave;'):
    global checks
    command = [str(java), '-Xcheck:jni']
    if brand_value:
        command.append('-Dfixture.brand=' + brand_value)
    command += ['-cp', str(folder / 'boot'), boot, *map(str, scopes)]
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        assert process.stdout.readline().strip() == 'FIXTURE_READY'
        def inspect(output, extra=()):
            result = subprocess.run([str(build / 'MappingAnalyzer.exe'), 'inspect', '--lite', '--identity-lite',
                                 '--required-only', '--detect-only', '--pid', str(process.pid),
                                 '--java', str(attach_java), '--helper', str(build / 'tools/McOverlayAttachHelper.jar'),
                                 '--probe', str(build / 'tools/MappingProbe-v17.dll'),
                                 '--native-loader', str(build / 'tools/McOverlayNativeLoader.exe'),
                                 '--pack', str(root / 'mapping/packs/default-v2.json'),
                                 '--contracts', str(root / 'mapping/contracts-v2.json'), '--out', str(output), *extra],
                                capture_output=True, text=True, timeout=90)
            assert result.returncode == 0, (name, result.stdout, result.stderr)
            return snapshot_from_stream(output)

        output = folder / (name + '.jsonl')
        snapshot = inspect(output)
        if candidates:
            assert all(not item['version'] for item in snapshot['runtimeEvidence']), snapshot
            assert all(not any(attempt['source'] == 'realms-semantic-candidate'
                               for attempt in item['attempts']) for item in snapshot['runtimeEvidence']), snapshot
            evidence = next(item for item in snapshot['runtimeEvidence'] if item['anchor'] == pin_anchor)
            loader = next(item for item in snapshot['loaderInstances'] if item['loaderKey'] == evidence['loaderKey'])
            snapshot = inspect(folder / (name + '-candidate.jsonl'), (
                '--runtime-anchor', pin_anchor, '--runtime-loader-type', loader['type'],
                '--runtime-loader-instance', str(int(loader['instance'])), '--evidence-candidates', '1.8.9'))
            pinned = [item for item in snapshot['loaderInstances']
                      if item['type'] == loader['type'] and item['instance'] == loader['instance']]
            assert len(pinned) == 1, snapshot['loaderInstances']
            assert all(item['loaderKey'] == pinned[0]['loaderKey']
                       for item in snapshot['runtimeEvidence']), snapshot
            checks += 4
        evidence = snapshot['runtimeEvidence']
        assert evidence and all(item['loaderKey'] for item in evidence)
        assert all(item['anchor'] in ('Lave;', 'Lenn;', 'Lnet/minecraft/client/main/Main;') for item in evidence)
        assert not any(item['family'] == 'Vanilla' for item in snapshot['launchEvidence'])
        assert all(item['attempts'] and all(set(('source', 'status', 'value', 'observed', 'reason')) <= set(attempt)
                                          for attempt in item['attempts']) for item in evidence), evidence
        assert all('brandObserved' in item for item in evidence), evidence
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
                         ('oversized', 'runtime version resource unreadable or exceeds 65536 bytes'),
                         ('invalid-id', 'unsupported runtime version resource id')]:
    invalid = capture(name, [folder / name])
    assert all(item['version'] == '1.8.9' and item['versionSource'] == 'realms-shared-constants' for item in invalid)
    assert all(item['diagnostic'] == 'unusable version.json ignored; ' + diagnostic for item in invalid)
    assert all(any(attempt['source'] == 'loader-version-json' and attempt['status'] == 'unavailable'
                   and attempt['reason'] == diagnostic for attempt in item['attempts']) for item in invalid)
    checks += 3
for name in ('malformed-no-legacy', 'oversized-no-legacy', 'invalid-id-no-legacy'):
    invalid = capture(name, [folder / name])
    assert all(not item['version'] and not item['versionSource'] for item in invalid)
    checks += 1

renamed = capture('renamed-version-positive', [folder / 'renamed-legacy'], 'lunar', candidates=True)
assert all(item['version'] == '1.8.9' and item['versionSource'] == 'realms-semantic-candidate' for item in renamed)
assert all(any(attempt['source'] == 'realms-shared-constants' and attempt['status'] == 'unavailable'
               and 'VERSION_STRING' in attempt['reason'] for attempt in item['attempts']) for item in renamed)
assert all(any(attempt['source'] == 'realms-semantic-candidate' and attempt['status'] == 'verified-value'
               and attempt['value'] == '1.8.9' for attempt in item['attempts']) for item in renamed)
checks += 3
for name, reason in [('renamed-wrong-protocol', 'protocol/tick discriminators'),
                     ('renamed-wrong-ticks', 'protocol/tick discriminators'),
                     ('renamed-extra-version', 'no unique exact 1.8.9'),
                     ('renamed-too-many-fields', 'exceeds 32'),
                     ('renamed-unreadable', 'unavailable')]:
    invalid = capture(name, [folder / name], 'lunar', candidates=True,
                      boot='ErroneousHolderBoot' if name == 'renamed-unreadable' else 'EvidenceBoot')
    assert all(not item['version'] and not item['versionSource'] for item in invalid), invalid
    assert all(any(attempt['source'] == 'realms-semantic-candidate' and attempt['status'] == 'unavailable'
                   and reason in attempt['reason'] for attempt in item['attempts']) for item in invalid), invalid
    checks += 2

observed = capture('observed-brand-preserved', [folder / 'legacy'], 'untrusted client brand')
assert all(item['brand'] == 'modified' and item['brandObserved'] == 'untrusted client brand' for item in observed)
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
delegated_candidate = capture('parent-candidate-holder-collision', [folder / 'renamed-legacy', folder / 'collider'],
                              'lunar', boot='DelegatingBoot', candidates=True)
assert all(not item['version'] for item in delegated_candidate), delegated_candidate
assert all(any(attempt['source'] == 'realms-semantic-candidate' and
               'different defining loader' in attempt['reason'] for attempt in item['attempts'])
           for item in delegated_candidate), delegated_candidate
checks += 2
print(f'{checks} runtime evidence checks passed; owned synthetic JVM: {java}')

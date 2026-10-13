"""Validate recorded Lunar 1.8.9 + Forge member metadata, without a live JVM.

The fixture contains real required classes and their complete hierarchy metadata.
It deliberately omits installed bytecodes, pools and process identities, so these
tests establish exact member/loader validation only, never live injection or
structural resolver acceptance. Run with the directory containing MappingAnalyzer.
"""
import copy
import gzip
import json
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
FIXTURE = Path(__file__).with_name('fixtures') / 'lunar-1.8.9-forge-required-metadata.json.gz'


def recorded_metadata():
    return json.loads(gzip.decompress(FIXTURE.read_bytes()))


def recorded_snapshot():
    """Wrap the recorded metadata in an offline Analyzer test envelope.

    Empty bytecodes/pools are test placeholders. They cannot reproduce the source
    installed-content proof and must not be used for automatic mapping acceptance.
    """
    fixture = recorded_metadata()
    classes = copy.deepcopy(fixture['classes'])
    for klass in classes:
        klass.update(major=52, minor=0, constantPoolCount=1, constantPool='')
        for method in klass['methods']:
            method['bytecode'] = ''
    return {
        'snapshotVersion': 1, 'complete': True,
        'captureKind': 'jvmti-installed-double-read',
        'pid': 1, 'processStart': 'recorded-lunar-forge-fixture',
        'requestId': 'recorded-metadata-test',
        'loaders': [{'id': 1, 'type': fixture['loaderType'], 'instance': 1}],
        'launchEvidence': [{'family': 'Lunar', 'confidence': 250}],
        'classes': classes,
    }


def main(build):
    folder = build / 'lunar-forge-fixtures'
    folder.mkdir(exist_ok=True)
    exe = build / 'MappingAnalyzer.exe'
    pack = ROOT / 'mapping/packs/default-v2.json'
    contracts = ROOT / 'mapping/contracts-v1.json'
    fixture = recorded_metadata()
    provenance = fixture['provenance']
    assert fixture['fixtureVersion'] == 1
    assert provenance['sourceSnapshotSha256'] == '0858951e82706a910a5fba7b90cbd56bdec9bce4d81a520af901817d5d7c5f3b'
    assert (provenance['sourceClassIndexCount'], provenance['sourceDetailClassCount'],
            provenance['sourceMappedClassCount']) == (19611, 402, 55)
    assert len(fixture['classes']) == provenance['requiredHierarchyClassCount'] == 70
    assert sum(len(c['fields']) for c in fixture['classes']) == provenance['requiredHierarchyFieldCount'] == 548
    assert sum(len(c['methods']) for c in fixture['classes']) == provenance['requiredHierarchyMethodCount'] == 3124
    assert sum(len(c['interfaces']) for c in fixture['classes']) == provenance['requiredHierarchyInterfaceCount'] == 62
    by_name = {c['name']: c for c in fixture['classes']}
    for name, fields, methods in [('Lnet/minecraft/client/Minecraft;', 120, 419),
                                  ('Lnet/minecraft/entity/Entity;', 84, 339),
                                  ('Lnet/minecraft/world/World;', 48, 311)]:
        assert (len(by_name[name]['fields']), len(by_name[name]['methods'])) == (fields, methods)
    assert 'Lnet/minecraftforge/common/capabilities/ICapabilitySerializable;' in by_name
    assert any(i['name'] == 'Lcom/replaymod/core/mixin/MinecraftAccessor;'
               for i in by_name['Lnet/minecraft/client/Minecraft;']['interfaces'])

    def write(name, value):
        path = folder / name
        path.write_text(json.dumps(value, separators=(',', ':')), encoding='utf-8')
        return path

    def run(*args, expected=0):
        result = subprocess.run([str(exe), *map(str, args)], capture_output=True, encoding='utf-8')
        assert result.returncode == expected, (args, result.returncode, result.stdout[-2000:], result.stderr)

    def validate(name, raw, expected=0):
        snapshot = folder / (name + '.json')
        run('inspect', '--snapshot', write(name + '-raw.json', raw), '--out', snapshot)
        output = folder / (name + '-validation.json')
        run('validate', '--pack', pack, '--contracts', contracts, '--snapshot', snapshot,
            '--out', output, expected=expected)
        return json.loads(output.read_text(encoding='utf-8'))

    raw = recorded_snapshot()
    good = validate('recorded', raw)
    assert good['valid'] and not good['injectionReady']
    assert not good['identity']['versionVerified']
    assert 'independent runtime' in good['reason']
    assert good['dictionary'] == 'minecraft-1.8.9-lunar-srg'
    symbols = good['pack']['providers'][0]['dictionaries'][0]['symbols']
    specs = json.loads(contracts.read_text(encoding='utf-8'))['symbols']
    assert specs['playerField']['required'] and specs['entityName']['required']
    required_receipts = [item for item in good['symbols'] if specs[item['symbol']]['required']]
    assert required_receipts and all(item['accepted'] for item in required_receipts)

    def player_field(snapshot):
        owner = next(c for c in snapshot['classes'] if c['name'] == 'Lnet/minecraft/client/Minecraft;')
        return next(f for f in owner['fields'] if f['name'] == symbols['playerField'])

    descriptor = copy.deepcopy(raw)
    player_field(descriptor)['descriptor'] = 'Ljava/lang/Object;'
    static = copy.deepcopy(raw)
    player_field(static)['modifiers'] ^= 8
    loader = copy.deepcopy(raw)
    loader['loaders'].append({'id': 2, 'type': fixture['loaderType'], 'instance': 2})
    next(c for c in loader['classes'] if c['name'] == 'Lnet/minecraft/entity/Entity;')['loader'] = 2
    missing = copy.deepcopy(raw)
    owner = next(c for c in missing['classes'] if c['name'] == 'Lnet/minecraft/client/Minecraft;')
    owner['fields'] = [f for f in owner['fields'] if f['name'] != symbols['playerField']]
    for name, changed in [('wrong-required-descriptor', descriptor), ('wrong-required-static', static),
                          ('wrong-required-loader', loader), ('missing-required-field', missing)]:
        rejected = validate(name, changed, expected=3)
        assert not rejected['valid'] and not rejected['injectionReady']

    duplicate = copy.deepcopy(raw)
    duplicate['loaders'].append({'id': 2, 'type': fixture['loaderType'], 'instance': 2})
    anchor = copy.deepcopy(next(c for c in duplicate['classes'] if c['name'] == 'Lnet/minecraft/client/Minecraft;'))
    anchor['loader'] = 2
    duplicate['classes'].append(anchor)
    rejected = validate('ambiguous-anchor-loader', duplicate, expected=3)
    assert not rejected['valid'] and not rejected['injectionReady']
    print('Recorded Lunar 1.8.9 + Forge: 70 hierarchy classes / 548 fields / 3124 methods; '
          'authored SRG validation passed; required descriptor/static/loader/missing-member '
          'and duplicate anchor rejected. Offline metadata only; no live-attach claim.')


if __name__ == '__main__':
    main(Path(sys.argv[1]).resolve())

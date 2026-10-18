"""Audit all legacy authored members against the exact official 1.8.9 client JAR.

Offline metadata only: no JVM, JNI, Hook or gameplay acceptance is implied.
Usage: Test-LegacyArtifact.py --client CLIENT_JAR [--out AUDIT_JSON]
"""
import argparse
import hashlib
import json
import re
import runpy
import zipfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--client', type=Path, required=True)
parser.add_argument('--out', type=Path)
args = parser.parse_args()
digest = hashlib.sha1(args.client.read_bytes()).hexdigest()
assert digest == '3870888a6c3d349d3771a3e9d16c9bf5e076b908', 'official 1.8.9 client SHA-1 mismatch'
ClassFile = runpy.run_path(str(root / 'mapping/tools/Generate-ModernPack.py'))['ClassFile']
pack = json.loads((root / 'mapping/packs/default-v2.json').read_text())
contracts = json.loads((root / 'mapping/contracts-v1.json').read_text())
dictionary = next(d for p in pack['providers'] if p['family'] == 'Vanilla'
                  for d in p['dictionaries'] if d['mappingNamespace'] == 'Notch')
symbols = dictionary['symbols']
jar = zipfile.ZipFile(args.client)
entries = set(jar.namelist())
cache = {}

def binary(name):
    return name[1:-1] if name.startswith('L') and name.endswith(';') else name.replace('.', '/')

def klass(name):
    name = binary(name)
    if name not in cache:
        cache[name] = ClassFile(jar.read(name + '.class')) if name + '.class' in entries else None
    return cache[name]

def member(owner, kind, name, descriptor, static):
    queue, seen = [binary(owner)], set()
    while queue:
        current = queue.pop()
        if current in seen:
            continue
        seen.add(current)
        cf = klass(current)
        if not cf:
            continue
        if any(m['name'] == name and m['descriptor'] == descriptor and bool(m['modifiers'] & 8) == static
               for m in (cf.fields if kind == 'field' else cf.methods)):
            return current
        queue.extend([cf.parent, *cf.interfaces])
    return ''

rows = []
for key, value in symbols.items():
    spec = contracts['symbols'][key]
    kind = spec['kind']
    row = {'symbol': key, 'kind': kind, 'required': spec.get('required', False), 'mapping': value}
    if kind == 'class':
        row['matched'] = klass(value) is not None
    elif kind in ('field', 'method'):
        if not value:
            row.update(matched=True, inactive=True, scope='empty optional mapping; alternate API is used')
            rows.append(row)
            continue
        owner = symbols[spec['owner']]
        descriptor = re.sub(r'\{([^}]+)\}', lambda m: symbols[m[1]], spec['descriptor'])
        names = value if isinstance(value, list) else [value]
        matches = [{'name': name, 'declaringClass': member(owner, kind, name, descriptor, spec.get('static', False))}
                   for name in names]
        row.update(owner=owner, descriptor=descriptor, static=spec.get('static', False), lookups=matches)
        row['matched'] = any(m['declaringClass'] for m in matches) if spec.get('alternatives') else all(m['declaringClass'] for m in matches)
    else:
        row['matched'] = True
        row['scope'] = 'descriptor/schema value, not a member lookup'
    rows.append(row)

assert member('bda', 'method', 'n', '()V', False) == 'bda'
assert not member('bda', 'method', 'j', '()V', False), 'wrong-descriptor historical regression must stay rejected'
assert not member('bda', 'method', 'n', '()Z', False)
assert member('bda', 'method', 'j', '()Z', False) == 'bda'
missing = [r for r in rows if not r['matched']]
result = {'evidence': 'offline official JAR metadata; no runtime or gameplay validation',
          'version': '1.8.9', 'clientSha1': digest, 'dictionary': dictionary['id'],
          'symbolCount': len(rows), 'matched': sum(r['matched'] for r in rows),
          'inactiveOptionalSymbols': sum(r.get('inactive', False) for r in rows),
          'configuredClassAndMemberChecks': sum(r['kind'] in ('class', 'field', 'method') and not r.get('inactive') for r in rows),
          'wrongDescriptorNegatives': 2, 'missing': missing, 'members': rows}
if args.out:
    args.out.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
print(json.dumps({k: v for k, v in result.items() if k != 'members'}))
assert not missing, 'nonempty authored mapping does not match official class/member descriptor: ' + repr(missing)

"""Generate the minimal 1.20.1 Vanilla dictionary from hash-pinned Mojang artifacts.

Inputs must be the official client_mappings and client.jar. No downloads, guessed
aliases, decompilation or runtime claims. Output includes an offline member audit.
"""
import argparse
import hashlib
import json
import re
import struct
import zipfile
from pathlib import Path

VERSION = '1.20.1'
MAPPINGS_SHA1 = '6c48521eed01fe2e8ecdadbd5ae348415f3c47da'
CLIENT_SHA1 = '0c3ec587af28e5a785c0b4a7b8a30f9a8f78f838'

class ClassFile:
    def __init__(self, data):
        self.data, self.pos = data, 0
        assert self.take(4) == bytes.fromhex('cafebabe')
        self.minor, self.major, count = self.u2(), self.u2(), self.u2()
        self.cp = [None] * count
        i = 1
        while i < count:
            tag = self.take(1)[0]
            if tag == 1:
                self.cp[i] = self.take(self.u2()).decode('utf-8', errors='replace')
            elif tag == 7:
                self.cp[i] = self.u2()
            else:
                self.take({3:4,4:4,5:8,6:8,8:2,9:4,10:4,11:4,12:4,15:3,16:2,17:4,18:4,19:2,20:2}[tag])
                if tag in (5,6): i += 1
            i += 1
        self.modifiers, this, parent = self.u2(), self.u2(), self.u2()
        self.name = self.cp[self.cp[this]]
        self.parent = self.cp[self.cp[parent]] if parent else ''
        self.interfaces = [self.cp[self.cp[self.u2()]] for _ in range(self.u2())]
        self.fields, self.methods = self.members(), self.members()

    def take(self, n):
        value = self.data[self.pos:self.pos+n]
        assert len(value) == n
        self.pos += n
        return value

    def u2(self): return struct.unpack('>H', self.take(2))[0]
    def u4(self): return struct.unpack('>I', self.take(4))[0]
    def members(self):
        result = []
        for _ in range(self.u2()):
            flags, name, descriptor = self.u2(), self.u2(), self.u2()
            result.append({'name':self.cp[name], 'descriptor':self.cp[descriptor], 'modifiers':flags})
            for _ in range(self.u2()):
                self.u2()
                self.take(self.u4())
        return result

def generate(mapping_path, client_path, output):
    for path, digest in ((mapping_path,MAPPINGS_SHA1),(client_path,CLIENT_SHA1)):
        if hashlib.sha1(path.read_bytes()).hexdigest() != digest:
            raise ValueError('official artifact SHA-1 mismatch: ' + str(path))
    classes, members, owner = {}, {}, ''
    for line in mapping_path.read_text(encoding='utf-8').splitlines():
        if not line or line.startswith('#'): continue
        if not line.startswith(' '):
            owner, runtime = line[:-1].split(' -> ')
            classes[owner] = runtime
            members[owner] = []
        else:
            declaration, runtime = line.strip().split(' -> ')
            declaration = re.sub(r'^\d+:\d+:', '', declaration)
            match = re.fullmatch(r'(\S+) (\S+)\(([^)]*)\)(?::\d+(?::\d+)?)?', declaration)
            if match:
                result, name, params = match.groups()
                members[owner].append(('method', name, result, params, runtime))
            else:
                result, name = declaration.split(' ')
                members[owner].append(('field', name, result, '', runtime))
    inverse = {v:k for k,v in classes.items()}
    jar = zipfile.ZipFile(client_path)
    cache = {}
    def klass(name):
        if name not in cache: cache[name] = ClassFile(jar.read(name.replace('.','/')+'.class'))
        return cache[name]
    def descriptor(name):
        if name.endswith('[]'): return '[' + descriptor(name[:-2])
        return {'void':'V','boolean':'Z','byte':'B','char':'C','short':'S','int':'I','long':'J','float':'F','double':'D'}.get(name) or 'L'+classes.get(name,name).replace('.','/')+';'
    symbols, contracts, audit = {}, {}, []
    def class_pair(key, name):
        symbols[key+'Name'], symbols[key+'Signature'] = classes[name], descriptor(name)
        contracts[key+'Name'] = {'kind':'class','required':True}
        contracts[key+'Signature'] = {'kind':'descriptor','required':False}
        klass(classes[name])
    logical = {
        'minecraft':'net.minecraft.client.Minecraft', 'player':'net.minecraft.client.player.LocalPlayer',
        'entityPlayer':'net.minecraft.world.entity.player.Player',
        'living':'net.minecraft.world.entity.LivingEntity', 'entity':'net.minecraft.world.entity.Entity',
        'aabb':'net.minecraft.world.phys.AABB', 'world':'net.minecraft.world.level.Level',
        'worldClient':'net.minecraft.client.multiplayer.ClientLevel', 'chatComponent':'net.minecraft.network.chat.Component',
        'sharedConstants':'net.minecraft.SharedConstants', 'worldVersion':'net.minecraft.WorldVersion',
        'mouseHandler':'net.minecraft.client.MouseHandler'}
    for key,name in logical.items(): class_pair(key,name)
    def find(owner, kind, name, params=''):
        matches = [m for m in members[owner] if m[:2] == (kind,name) and m[3] == params]
        if len(matches) == 1: return matches[0]
        cf = klass(classes[owner])
        if cf.parent.replace('/','.') in inverse:
            return find(inverse[cf.parent.replace('/','.')],kind,name,params)
        raise ValueError(f'no unique official member: {owner}.{name}({params})')
    def add(key, owner_key, kind, names, params='', static=False):
        values, descs = [], []
        for name in names if isinstance(names,list) else [names]:
            _, _, result, _, runtime = find(logical[owner_key],kind,name,params)
            desc = '('+''.join(descriptor(t) for t in params.split(',') if t)+')'+descriptor(result) if kind=='method' else descriptor(result)
            todo, seen, found = [classes[logical[owner_key]]], set(), False
            while todo:
                candidate = todo.pop()
                if candidate in seen or candidate.replace('.','/')+'.class' not in jar.namelist(): continue
                seen.add(candidate); cf = klass(candidate)
                found |= any(m['name']==runtime and m['descriptor']==desc and bool(m['modifiers']&8)==static for m in getattr(cf,'methods' if kind=='method' else 'fields'))
                todo.extend(x.replace('/','.') for x in [cf.parent,*cf.interfaces] if x)
            if not found: raise ValueError(f'JAR descriptor/modifier mismatch: {key} {runtime} {desc}')
            values.append(runtime); descs.append(desc)
            audit.append({'logical':key,'officialOwner':logical[owner_key],'officialMember':name,'runtimeMember':runtime,'descriptor':desc,'static':static})
        assert len(set(descs)) == 1
        symbols[key] = values if isinstance(names,list) else values[0]
        # Preserve descriptor resolution through the Mapping contract, including mapped types.
        desc = descs[0]
        for k in logical:
            desc = desc.replace(symbols[k+'Signature'],'{'+k+'Signature}')
        contracts[key] = {'kind':kind,'owner':owner_key+'Name','descriptor':desc,'static':static,'required':True}
    for args in [
        ('getMinecraft','minecraft','method','getInstance','',True),
        ('playerField','minecraft','field','player'),('worldField','minecraft','field','level'),
        ('getHealth','living','method','getHealth'),('getMaxHealth','living','method','getMaxHealth'),
        ('getEntityId','entity','method','getId'),('getBounds','entity','method','getBoundingBox'),
        ('isMainThread','minecraft','method','isSameThread'),('isSingleplayer','minecraft','method','hasSingleplayerServer'),
        ('getLoadedEntities','worldClient','method','entitiesForRendering'),
        ('getName','entity','method','getName'),('getFormattedText','chatComponent','method','getString'),
        ('getEyeHeight','entity','method','getEyeHeight'),('isInvisible','entity','method','isInvisible'),
        ('isAlive','entity','method','isAlive'),('isRemoved','entity','method','isRemoved'),
        ('currentScreenField','minecraft','field','screen'),
        ('positionMethods','entity','method',['getX','getY','getZ']),
        ('previousPositionFields','entity','field',['xo','yo','zo']),
        ('rotationMethods','entity','method',['getYRot','getXRot']),
        ('aabbFields','aabb','field',['minX','minY','minZ','maxX','maxY','maxZ']),
        ('getCurrentVersion','sharedConstants','method','getCurrentVersion','',True),
        ('getVersionName','worldVersion','method','getName'),
        ('mouseHandlerField','minecraft','field','mouseHandler'),
        ('grabMouse','mouseHandler','method','grabMouse'),('releaseMouse','mouseHandler','method','releaseMouse'),
        ('isMouseGrabbed','mouseHandler','method','isMouseGrabbed')]: add(*args)
    symbols['guiScreenSignature'] = descriptor('net.minecraft.client.gui.screens.Screen')
    contracts['guiScreenSignature'] = {'kind':'descriptor','required':False}
    # Do not ship a Forge/SRG dictionary based solely on Vanilla Mojang names.
    detection = [{'match':0,'value':symbols['minecraftSignature'],'confidence':10}]
    dictionary = {'id':'vanilla-1.20.1-official','label':'Vanilla 1.20.1 (official obfuscated, read-only)',
                  'family':'Vanilla','mappingNamespace':'Notch','detection':detection,'symbols':symbols}
    pack = {'schemaVersion':2,'packVersion':1,'id':'arcveil-vanilla-1.20.1','gameVersion':VERSION,
            'source':f'Mojang client_mappings SHA1 {MAPPINGS_SHA1}; client.jar SHA1 {CLIENT_SHA1}; offline member-checked, runtime validation required',
            'providers':[{'id':'vanilla-modern','family':'Vanilla','priority':100,'detection':detection,'dictionaries':[dictionary]}]}
    contract = {'contractVersion':1,'symbols':contracts,'requiredAlternatives':[], 'constructors':[]}
    output.mkdir(parents=True,exist_ok=True)
    for name,value in [('vanilla-1.20.1.json',pack),('contracts-1.20.1.json',contract),('vanilla-1.20.1-provenance.json',{
        'version':VERSION,'javaMajor':17,'clientSha1':CLIENT_SHA1,'mappingsSha1':MAPPINGS_SHA1,
        'clientUrl':f'https://piston-data.mojang.com/v1/objects/{CLIENT_SHA1}/client.jar',
        'mappingsUrl':f'https://piston-data.mojang.com/v1/objects/{MAPPINGS_SHA1}/client.txt',
        'validation':'offline official JAR metadata only; no JVM/Attach/gameplay verification','members':audit})]:
        (output/name).write_text(json.dumps(value,indent=2)+'\n',encoding='utf-8')
    print(f'1.20.1: {len(symbols)} logical keys, {len(audit)} exact JAR member checks passed (offline only)')

if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mappings',type=Path,required=True)
    parser.add_argument('--client',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args()
    generate(args.mappings,args.client,args.out)

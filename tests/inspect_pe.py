"""Read-only PE evidence harness. Python standard library only; never loads the DLL/game.

Observed bytes are evidence for constructing a profile, NOT independent validation.
Pass --profile JSON to independently compare known expected bytes/fingerprints.
"""
import argparse
import datetime
import hashlib
import json
import re
import struct
from pathlib import Path


class PE:
    def __init__(self, path):
        self.data = Path(path).read_bytes()
        if self.data[:2] != b'MZ':
            raise ValueError('Missing MZ')
        pe = self.u32(0x3c)
        if self.data[pe:pe+4] != b'PE\0\0':
            raise ValueError('Missing PE signature')
        coff = pe + 4
        self.machine, count, self.timestamp = struct.unpack_from('<HHI', self.data, coff)
        optsize = self.u16(coff+16)
        opt = coff+20
        if self.u16(opt) != 0x20b:
            raise ValueError('Only PE32+ supported')
        self.base = self.u64(opt+24)
        self.image_size = self.u32(opt+56)
        self.headers_size = self.u32(opt+60)
        self.exception_rva, self.exception_size = struct.unpack_from('<II', self.data, opt+112+3*8)
        self.sections = []
        for n in range(count):
            s = opt+optsize+n*40
            name = self.data[s:s+8].rstrip(b'\0').decode('ascii')
            vsize, rva, size, offset = struct.unpack_from('<IIII', self.data, s+8)
            self.sections.append(dict(name=name, rva=rva, virtual_size=vsize, raw_size=size, raw_offset=offset))
        self.functions = []
        for i in range(self.exception_size//12):
            begin, end, unwind = struct.unpack('<III', self.read(self.exception_rva+i*12, 12))
            if begin:
                self.functions.append((begin, end, unwind))

    def u16(self, o): return struct.unpack_from('<H', self.data, o)[0]
    def u32(self, o): return struct.unpack_from('<I', self.data, o)[0]
    def u64(self, o): return struct.unpack_from('<Q', self.data, o)[0]
    def offset(self, rva, size=1):
        if 0 <= rva and rva+size <= self.headers_size:
            return rva
        for s in self.sections:
            d = rva-s['rva']
            if 0 <= d and d+size <= s['raw_size']:
                return s['raw_offset']+d
        raise ValueError(f'RVA has no raw bytes: {rva:#x} length {size}')
    def read(self, rva, size):
        o = self.offset(rva, size)
        return self.data[o:o+size]
    def boundary(self, rva):
        return [dict(begin=hex(a), end=hex(b), unwind=hex(c), is_entry=a == rva)
                for a, b, c in self.functions if a <= rva < b]


HOOKS = {'ui': (0x382760, 13), 'camera': (0x2b26a0, 18),
         'waypoints': (0x30e650, 15), 'vulkan_init': (0xfdd960, 15),
         'render': (0xfd13d0, 16)}
REGISTRY = {'ui': (0x1dda570, 'local_player_ui_render_setup'),
            'camera': (0x1e01010, 'player_camera_transform'),
            'waypoints': (0x1e0af80, 'player_waypoints_ui')}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe', required=True)
    p.add_argument('--out', required=True)
    p.add_argument('--source', help='Optional dllmain.cpp: count literal Expected patterns in .text')
    p.add_argument('--profile', help='JSON with sha256, text_sha256 and hooks {name: {rva, expected_hex}}')
    args = p.parse_args()
    pe = PE(args.exe)
    text = next(s for s in pe.sections if s['name'] == '.text')
    code = pe.read(text['rva'], text['raw_size'])
    result = dict(verification_level='static_file_only; no runtime or ABI validation',
                  captured_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  exe=str(Path(args.exe).resolve()), sha256=hashlib.sha256(pe.data).hexdigest(),
                  file_bytes=len(pe.data), machine=hex(pe.machine), image_base=hex(pe.base),
                  pe_timestamp=hex(pe.timestamp), size_of_image=hex(pe.image_size),
                  text_sha256=hashlib.sha256(code).hexdigest(), sections=pe.sections,
                  exception_entry_count=len(pe.functions), hooks={}, registry={}, vulkan_xrefs={})
    for name, (rva, size) in HOOKS.items():
        result['hooks'][name] = dict(rva=hex(rva), observed_hex=pe.read(rva, size).hex(' '),
                                     pdata=pe.boundary(rva))
    for key, (rva, expected_name) in REGISTRY.items():
        ptr, length, fn = struct.unpack('<QQQ', pe.read(rva, 24))
        name = pe.read(ptr-pe.base, length).decode('utf8') if length < 1024 else '<invalid length>'
        result['registry'][key] = dict(rva=hex(rva), name=name, name_matches=name == expected_name,
                                       length=length, function_rva=hex(fn-pe.base),
                                       function_matches=fn-pe.base == HOOKS[key][0])
    for name, rva in [('vkCreateSwapchainKHR', 0xfe0973), ('vkGetSwapchainImagesKHR', 0xfe09ad), ('vkQueuePresentKHR', 0xfe09e7)]:
        chunk = pe.read(rva, 64)
        # LEA R9,[RIP+disp32], call resolver, then MOV [RDI+offset],RAX.
        target = rva+7+struct.unpack_from('<i', chunk, 3)[0] if chunk[:3] == b'\x4c\x8d\x0d' else None
        observed = pe.read(target, len(name)+1).rstrip(b'\0').decode('ascii') if target else None
        call = chunk.find(b'\xe8', 7)
        store = re.search(b'\x48\x89\x87(.{4})', chunk[call+5:], re.S) if call >= 0 else None
        result['vulkan_xrefs'][name] = dict(rva=hex(rva), bytes_hex=chunk.hex(' '),
            referenced_name=observed, name_matches=observed == name,
            table_offset=hex(struct.unpack('<I', store.group(1))[0]) if store else None)
    if args.source:
        source = Path(args.source).read_text(encoding='utf-8-sig')
        result['source_patterns'] = {}
        for m in re.finditer(r'(g_\w+Expected)\s*=\s*\{([^}]+)\}', source):
            pattern = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})\b', m[2]))
            if pattern:
                matches = [hex(text['rva']+i) for i in range(len(code)) if code.startswith(pattern, i)]
                result['source_patterns'][m[1]] = dict(expected_hex=pattern.hex(' '), match_count=len(matches), rvas=matches[:100])
    if args.profile:
        profile = json.loads(Path(args.profile).read_text(encoding='utf-8-sig'))
        checks = {key: result[key] == profile[key] for key in ('sha256', 'text_sha256') if key in profile}
        for name, item in profile.get('hooks', {}).items():
            expected = bytes.fromhex(item['expected_hex'])
            checks[name] = pe.read(int(item['rva'], 0), len(expected)) == expected
        result['independent_profile_checks'] = checks
        result['independent_profile_pass'] = bool(checks) and all(checks.values())
    Path(args.out).write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n', encoding='utf8')
    print(json.dumps({k: result[k] for k in ('sha256', 'text_sha256', 'size_of_image', 'registry', 'vulkan_xrefs')}, indent=2))
    return 1 if result.get('independent_profile_pass') is False else 0


if __name__ == '__main__':
    raise SystemExit(main())


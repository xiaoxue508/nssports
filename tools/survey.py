"""Comprehensive static survey of Nintendo Switch Sports v0 ExeFS, main.npdm, and RomFS.

Compares undefined imports against host hooks in refs/totk-pc-recomp/runtime/ and refs/nxnative/dm/host/.
Outputs build/survey_v0.json and build/survey_v0.md.

Run under .venv-py314:
    .venv-py314\\Scripts\\python.exe tools\\survey.py
"""
import collections
import json
import re
import struct
import sys
import tomllib
from pathlib import Path

import lz4.block

ROOT = Path(__file__).resolve().parent.parent

DT_NULL, DT_NEEDED, DT_PLTRELSZ, DT_HASH, DT_STRTAB, DT_SYMTAB, DT_RELA, DT_RELASZ = 0, 1, 2, 4, 5, 6, 7, 8
DT_STRSZ, DT_INIT, DT_JMPREL, DT_INIT_ARRAY, DT_INIT_ARRAYSZ, DT_RELACOUNT = 10, 12, 23, 25, 27, 0x6FFFFFF9
DT_RELRSZ, DT_RELR, DT_GNU_HASH = 0x23, 0x24, 0x6FFFFEF5


class Nso:
    def __init__(self, path: Path):
        raw = path.read_bytes()
        assert raw[:4] == b'NSO0', f'{path} is not NSO0'
        flags = struct.unpack_from('<I', raw, 0xC)[0]
        self.build_id = raw[0x40:0x60].rstrip(b'\0').hex()
        segs = []
        for i in range(3):
            file_off, mem_off, dec_size = struct.unpack_from('<III', raw, 0x10 + i * 0x10)
            comp_size = struct.unpack_from('<I', raw, 0x60 + i * 4)[0]
            data = raw[file_off:file_off + comp_size]
            if flags & (1 << i):
                data = lz4.block.decompress(data, uncompressed_size=dec_size)
            segs.append((mem_off, data))
        self.bss_size = struct.unpack_from('<I', raw, 0x3C)[0]
        self.text = (segs[0][0], len(segs[0][1]))
        self.rodata = (segs[1][0], len(segs[1][1]))
        self.data = (segs[2][0], len(segs[2][1]))
        end = segs[2][0] + len(segs[2][1]) + self.bss_size
        self.size = (end + 0xFFF) & ~0xFFF
        img = bytearray(self.size)
        for off, d in segs:
            img[off:off + len(d)] = d
        self.image = bytes(img)
        self._parse_mod0()

    def u32(self, off):
        return struct.unpack_from('<I', self.image, off)[0]

    def u64(self, off):
        return struct.unpack_from('<Q', self.image, off)[0]

    def cstr(self, off):
        end = self.image.index(b'\0', off)
        return self.image[off:end].decode('utf-8', 'replace')

    def _parse_mod0(self):
        mod0 = self.u32(4)
        assert self.image[mod0:mod0 + 4] == b'MOD0'
        rel = lambda i: mod0 + struct.unpack_from('<i', self.image, mod0 + 4 + i * 4)[0]
        self.dynamic, self.bss_start, self.bss_end = rel(0), rel(1), rel(2)
        dyn = {}
        needed = []
        off = self.dynamic
        while True:
            tag, val = self.u64(off), self.u64(off + 8)
            off += 16
            if tag == DT_NULL:
                break
            if tag == DT_NEEDED:
                needed.append(val)
            dyn.setdefault(tag, val)
        self.dyn = dyn
        strtab = dyn.get(DT_STRTAB, 0)
        self.needed = [self.cstr(strtab + n) for n in needed]
        self.syms = []
        nsyms = self.symbol_count()
        if nsyms and DT_SYMTAB in dyn:
            st = dyn[DT_SYMTAB]
            for i in range(nsyms):
                o = st + i * 24
                name, info, _other, shndx = struct.unpack_from('<IBBH', self.image, o)
                value, size = struct.unpack_from('<QQ', self.image, o + 8)
                self.syms.append((self.cstr(strtab + name), info, shndx, value, size))
        self.relocs = []
        for base_tag, size_tag in ((DT_RELA, DT_RELASZ), (DT_JMPREL, DT_PLTRELSZ)):
            if base_tag in dyn:
                b, n = dyn[base_tag], dyn[size_tag] // 24
                for i in range(n):
                    r_off, r_info, r_add = struct.unpack_from('<QQq', self.image, b + i * 24)
                    self.relocs.append((r_off, r_info & 0xFFFFFFFF, r_info >> 32, r_add))
        if DT_RELR in dyn:
            where = 0
            for i in range(dyn[DT_RELRSZ] // 8):
                e = self.u64(dyn[DT_RELR] + i * 8)
                if e & 1 == 0:
                    self.relocs.append((e, 1027, 0, self.u64(e)))
                    where = e + 8
                else:
                    for k in range(63):
                        if (e >> (k + 1)) & 1:
                            a = where + k * 8
                            self.relocs.append((a, 1027, 0, self.u64(a)))
                    where += 63 * 8

    def symbol_count(self):
        dyn = self.dyn
        if DT_HASH in dyn:
            return self.u32(dyn[DT_HASH] + 4)
        if DT_GNU_HASH not in dyn:
            return 0
        h = dyn[DT_GNU_HASH]
        nbuckets, symoffset, bloom_size = self.u32(h), self.u32(h + 4), self.u32(h + 8)
        buckets = h + 16 + bloom_size * 8
        last = max([self.u32(buckets + 4 * i) for i in range(nbuckets)] + [0])
        if last < symoffset:
            return symoffset
        chains = buckets + 4 * nbuckets
        while not self.u32(chains + 4 * (last - symoffset)) & 1:
            last += 1
        return last + 1

    def imports(self):
        return [s[0] for s in self.syms if s[0] and s[2] == 0]

    def exports(self):
        return [s[0] for s in self.syms if s[0] and s[2] != 0]


def parse_npdm(path: Path):
    data = path.read_bytes()
    assert data[:4] == b'META', 'Invalid main.npdm'
    flags = data[0x0C]
    is_64bit = bool(flags & 1)
    addr_space = (flags >> 1) & 7
    addr_names = {0: '32-bit', 1: '36-bit', 2: '32-bit-no-map', 3: '39-bit'}
    main_prio = data[0x0E]
    main_core = data[0x0F]
    sysres_size = struct.unpack_from('<I', data, 0x14)[0]
    version = struct.unpack_from('<I', data, 0x18)[0]
    stack_size = struct.unpack_from('<I', data, 0x1C)[0]
    name = data[0x20:0x30].split(b'\0', 1)[0].decode('utf-8', 'replace')
    aci0_off, aci0_size = struct.unpack_from('<II', data, 0x70)
    program_id = None
    svc_count = 0
    if aci0_off and data[aci0_off:aci0_off + 4] == b'ACI0':
        program_id = f'{struct.unpack_from("<Q", data, aci0_off + 0x10)[0]:016X}'
        kcap_off, kcap_size = struct.unpack_from('<II', data, aci0_off + 0x30)
        for i in range(0, kcap_size, 4):
            cap = struct.unpack_from('<I', data, aci0_off + kcap_off + i)[0]
            if (cap & 0xF) == 0x7:  # EnableInterrupts/SVCs mask (0x0000000F -> 4 bits low == 7 means SVC mask)
                svc_count += bin((cap >> 5) & 0xFFFFFF).count('1')
    return {
        'title_name': name,
        'program_id': program_id,
        'is_64bit': is_64bit,
        'address_space': addr_names.get(addr_space, str(addr_space)),
        'main_thread_priority': main_prio,
        'main_thread_core': main_core,
        'main_thread_stack_size': hex(stack_size),
        'system_resource_size': hex(sysres_size),
        'version': version,
        'allowed_svcs_bits': svc_count,
    }


def collect_totk_hooks(runtime_dir: Path):
    hooks = set()
    for p in sorted(runtime_dir.glob('*.c')):
        txt = p.read_text(encoding='utf-8', errors='replace')
        macros = {}
        for m in re.finditer(r'^\s*#define\s+(\w+)\s+"([^"]+)"', txt, re.M):
            macros[m.group(1)] = m.group(2)
        for m in re.finditer(r'\bHOST_(?:OVERRIDE|OBSERVER)\(\s*([^,]+?)\s*,', txt):
            arg = m.group(1).strip()
            if arg.startswith('"') and arg.endswith('"'):
                hooks.add(arg[1:-1])
            elif arg in macros:
                hooks.add(macros[arg])
    # Also check nvn_procs.inc if any nvn* names are registered
    inc = runtime_dir / 'nvn_procs.inc'
    if inc.is_file():
        for m in re.finditer(r'"(nvn\w+)"', inc.read_text(encoding='utf-8', errors='replace')):
            hooks.add(m.group(1))
    return hooks


def collect_nxnative_hooks(host_dir: Path):
    hooks = set()
    for p in sorted(host_dir.glob('*.c')):
        txt = p.read_text(encoding='utf-8', errors='replace')
        for m in re.finditer(r'\bHLE(?:_FORCE(?:_DIRECT|_ALIAS)?)?\(\s*"([^"]+)"', txt):
            hooks.add(m.group(1))
    return hooks


def sym_area(s: str) -> str:
    m = re.match(r'_ZN(?:K)?2nn(\d+)(\w+)', s)
    if m:
        return 'nn::' + m.group(2)[:int(m.group(1))]
    m2 = re.match(r'_ZN(?:K)?2nv(\d+)(\w+)', s)
    if m2:
        return 'nv::' + m2.group(2)[:int(m2.group(1))]
    if s.startswith('_ZN2nv'):
        return 'nv'
    if s.startswith('nvn'):
        return 'nvn'
    if s.startswith('_Z'):
        return 'other_cpp'
    return 'C_libc'


def demangle_simple(s: str) -> str:
    m = re.match(r'_ZN(K)?2nn(\d+)(\w+)(.*)', s)
    if not m:
        return s
    n = int(m.group(2))
    ns = m.group(3)[:n]
    rest = m.group(3)[n:] + m.group(4)
    parts = [ns]
    pos = 0
    while pos < len(rest):
        if rest[pos] == 'E':
            break
        mm = re.match(r'(\d+)', rest[pos:])
        if mm:
            ln = int(mm.group(1))
            pos += len(mm.group(1))
            parts.append(rest[pos:pos + ln])
            pos += ln
        else:
            break
    return 'nn::' + '::'.join(parts) + (f' ({s})' if len(parts) > 1 else '')


def main():
    cfg = tomllib.loads((ROOT / 'config.local.toml').read_text(encoding='utf-8'))
    work = Path(cfg.get('work', {}).get('dir', 'build'))
    if not work.is_absolute():
        work = ROOT / work

    exefs_dir = work / 'extracted' / 'v0' / 'exefs'
    romfs_dir = work / 'extracted' / 'v0' / 'romfs'

    npdm_info = parse_npdm(exefs_dir / 'main.npdm')
    mods = {}
    all_imports = set()
    sdk_exports = set()
    markers_found = collections.defaultdict(list)
    sdk_versions = set()
    sdk_comps = set()

    marker_patterns = [
        (b'nvnBootstrapLoader', 'NVN'),
        (b'vkCreateInstance', 'Vulkan'),
        (b'eglGetProcAddress', 'OpenGL/EGL'),
        (b'sead::', 'sead'),
        (b'agl::', 'agl'),
        (b'gsys::', 'gsys'),
        (b'Havok', 'Havok'),
        (b'NintendoSDK_nnSdk', 'nnSdk'),
        (b'nn::mii', 'nn::mii'),
        (b'nn::npln', 'nn::npln'),
        (b'nn::nex', 'nn::nex'),
        (b'nn::friends', 'nn::friends'),
        (b'nn::bcat', 'nn::bcat'),
        (b'nn::nifm', 'nn::nifm'),
        (b'nn::hid', 'nn::hid'),
        (b'FSR', 'AMD FSR'),
        (b'FidelityFX', 'FidelityFX'),
    ]

    for name in ('main', 'subsdk0', 'subsdk1', 'subsdk2', 'sdk', 'rtld'):
        p = exefs_dir / name
        if not p.is_file():
            continue
        m = Nso(p)
        imps = m.imports()
        exps = m.exports()
        if name in ('main', 'subsdk0', 'subsdk1', 'subsdk2'):
            all_imports.update(imps)
        if name == 'sdk':
            sdk_exports.update(exps)
        rel_counts = collections.Counter(r[1] for r in m.relocs)
        mods[name] = {
            'build_id': m.build_id,
            'image_size': hex(m.size),
            'text': [hex(m.text[0]), hex(m.text[1])],
            'rodata': [hex(m.rodata[0]), hex(m.rodata[1])],
            'data': [hex(m.data[0]), hex(m.data[1])],
            'bss_size': hex(m.bss_size),
            'init': hex(m.dyn[DT_INIT]) if DT_INIT in m.dyn else None,
            'init_array_entries': m.dyn[DT_INIT_ARRAYSZ] // 8 if DT_INIT_ARRAYSZ in m.dyn else 0,
            'needed': m.needed,
            'relocs': {str(k): v for k, v in rel_counts.items()},
            'imports_count': len(imps),
            'exports_count': len(exps),
        }
        for pat, label in marker_patterns:
            if pat in m.image:
                markers_found[label].append(name)
        for match in re.findall(rb'NintendoSDK_nnSdk-(?:[a-zA-Z]+_)?([0-9_]+)', m.image):
            sdk_versions.add(match.decode('ascii', 'replace'))
        for match in re.findall(rb'SDK MW\+Nintendo\+([\w]+-[0-9_]+)', m.image):
            sdk_comps.add(match.decode('ascii', 'replace'))

    totk_hooks = collect_totk_hooks(ROOT / 'refs' / 'totk-pc-recomp' / 'runtime')
    nx_hooks = collect_nxnative_hooks(ROOT / 'refs' / 'nxnative' / 'dm' / 'host')

    # Classify undefined imports of main/subsdk*
    missing_from_totk = sorted(s for s in all_imports if s not in totk_hooks)
    # Exclude pure C++ std/game symbols exported by subsdk0/sdk only? Check which imports are NOT exported by sdk/subsdk0/main
    all_defined = set(sdk_exports)
    for name in ('main', 'subsdk0', 'subsdk1', 'subsdk2'):
        p = exefs_dir / name
        if p.is_file():
            all_defined.update(Nso(p).exports())

    # Specifically inspect nn:: / nv:: / nvn* / C_libc imports vs totk_hooks
    nn_imports = sorted(s for s in all_imports if sym_area(s).startswith(('nn::', 'nv', 'nvn')))
    nn_covered_totk = sorted(s for s in nn_imports if s in totk_hooks)
    nn_missing_totk = sorted(s for s in nn_imports if s not in totk_hooks)
    nn_missing_covered_by_nxnative = sorted(s for s in nn_missing_totk if s in nx_hooks)

    by_area_total = collections.Counter(sym_area(s) for s in nn_imports)
    by_area_missing = collections.Counter(sym_area(s) for s in nn_missing_totk)
    missing_grouped = collections.defaultdict(list)
    for s in nn_missing_totk:
        missing_grouped[sym_area(s)].append({
            'mangled': s,
            'readable': demangle_simple(s),
            'in_nxnative': s in nx_hooks,
        })

    # Inspect C/libc imports missing from totk_hooks (note: musl libc is inside sdk NSO, only check if unresolved against sdk!)
    unresolved_vs_all_mods = sorted(s for s in all_imports if s not in all_defined)
    c_missing = sorted(s for s in all_imports if sym_area(s) == 'C_libc' and s not in totk_hooks and s not in all_defined)

    # Inspect RomFS
    index = json.loads((romfs_dir / 'index.json').read_text(encoding='utf-8'))
    files = index['files']
    ext_counts = collections.Counter()
    ext_bytes = collections.Counter()
    top_counts = collections.Counter()
    top_bytes = collections.Counter()
    magic_counts = collections.Counter()
    nros_or_nrrs = []
    shader_files = []
    video_files = []
    audio_files = []

    with (romfs_dir / 'romfs.bin').open('rb') as rf:
        for idx_i, entry in enumerate(files):
            path = entry['path']
            size = entry['size']
            parts = path.split('/')
            top = parts[0] if len(parts) > 1 else '(root)'
            top_counts[top] += 1
            top_bytes[top] += size
            # double suffix if .zs
            if path.endswith('.zs'):
                stem = path[:-3]
                sfx = Path(stem).suffix.lower() + '.zs'
            else:
                sfx = Path(path).suffix.lower() or '(none)'
            ext_counts[sfx] += 1
            ext_bytes[sfx] += size

            low = path.lower()
            if low.endswith(('.nro', '.nrr')):
                nros_or_nrrs.append((path, size))
            if any(k in low for k in ('.sharcb', '.bnsh', '.bahs', '.bfsha')):
                shader_files.append((path, size))
            if any(k in low for k in ('.webm', '.mp4', '.usm', '.ivf')):
                video_files.append((path, size))
            if any(k in low for k in ('.bars', '.bwav', '.bfstm', '.bfwav', '.bfsar', '.bnk', '.pck')):
                audio_files.append((path, size))

            if idx_i < 2000 or low.endswith(('.sharcb', '.bnsh', '.bahs', '.bfsha', '.zs', '.sarc', '.pack')):
                if size >= 4:
                    rf.seek(entry['offset'])
                    head = rf.read(min(8, size))
                    if head[:4] == b'\x28\xb5\x2f\xfd':
                        magic = 'zstd'
                    elif head[:4] in (b'SARC', b'BNSH', b'BAHS', b'FRES', b'BNTX', b'BWAV', b'FSAR', b'Yaz0', b'FSHA'):
                        magic = head[:4].decode('ascii', 'replace')
                    elif head[:4] == b'\x1a\x45\xdf\xa3':
                        magic = 'WebM/EBML'
                    else:
                        magic = head[:4].hex()
                    magic_counts[magic] += 1

    report = {
        'npdm': npdm_info,
        'sdk_versions': sorted(sdk_versions),
        'sdk_components': sorted(sdk_comps),
        'markers': dict(markers_found),
        'modules': mods,
        'unresolved_vs_all_mods': unresolved_vs_all_mods,
        'c_libc_unresolved_and_unhooked': c_missing,
        'nn_imports_summary': {
            'total': len(nn_imports),
            'covered_by_totk': len(nn_covered_totk),
            'missing_from_totk': len(nn_missing_totk),
            'missing_but_in_nxnative': len(nn_missing_covered_by_nxnative),
            'by_area': {k: {'total': by_area_total[k], 'missing_from_totk': by_area_missing[k]}
                        for k in sorted(by_area_total, key=lambda x: (-by_area_missing[x], -by_area_total[x]))},
        },
        'missing_nn_by_area': dict(missing_grouped),
        'romfs': {
            'file_count': len(files),
            'total_bytes': index['romfs_size'],
            'top_dirs': {k: {'files': top_counts[k], 'mb': round(top_bytes[k] / 1048576, 2)}
                         for k, _ in top_counts.most_common()},
            'extensions': {k: {'files': ext_counts[k], 'mb': round(ext_bytes[k] / 1048576, 2)}
                           for k, _ in ext_counts.most_common(30)},
            'sampled_magics': dict(magic_counts.most_common(20)),
            'nros_or_nrrs': nros_or_nrrs,
            'shader_file_count': len(shader_files),
            'shader_files_sample': shader_files[:40],
            'video_file_count': len(video_files),
            'video_files_sample': video_files[:20],
            'audio_file_count': len(audio_files),
            'audio_files_sample': audio_files[:20],
        },
    }

    (work / 'survey_v0.json').write_text(json.dumps(report, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')

    # Render concise markdown report
    md = [
        '# Nintendo Switch Sports (`0100D2F00D5C0000`) v0 Survey',
        '',
        '## 1. `main.npdm`',
        f"- Title: `{npdm_info['title_name']}` | Program ID: `{npdm_info['program_id']}` | 64-bit: `{npdm_info['is_64bit']}` | Address space: `{npdm_info['address_space']}`",
        f"- Main Thread: priority `{npdm_info['main_thread_priority']}`, core `{npdm_info['main_thread_core']}`, stack `{npdm_info['main_thread_stack_size']}`",
        f"- SystemResourceSize: `{npdm_info['system_resource_size']}` | Version: `{npdm_info['version']}`",
        '',
        '## 2. ExeFS Modules & SDK',
        f"- SDK Versions found: {', '.join(sorted(sdk_versions)) or 'unknown'}",
        f"- Engine markers: {json.dumps(dict(markers_found))}",
        '',
        '| Module | Build ID (16) | Image Size | Text | Data | BSS | init_array | Relocs | Imports | Exports |',
        '|---|---|---|---|---|---|---:|---|---:|---:|',
    ]
    for name, info in mods.items():
        md.append(
            f"| `{name}` | `{info['build_id'][:16]}` | `{info['image_size']}` | `{info['text'][1]}` | "
            f"`{info['data'][1]}` | `{info['bss_size']}` | {info['init_array_entries']} | "
            f"`{info['relocs']}` | {info['imports_count']} | {info['exports_count']} |"
        )
    md += [
        '',
        f"- Symbols unresolved across all ExeFS modules: {len(unresolved_vs_all_mods)} -> {unresolved_vs_all_mods[:20]}",
        '',
        '## 3. `nn::*` / `nv::*` / `nvn*` Import Coverage vs `totk-pc-recomp`',
        f"- Total `nn/nv/nvn` imports in game modules: **{len(nn_imports)}**",
        f"- Already hooked in `totk-pc-recomp`: **{len(nn_covered_totk)}**",
        f"- Missing from `totk-pc-recomp`: **{len(nn_missing_totk)}** (of which **{len(nn_missing_covered_by_nxnative)}** exist in `refs/nxnative`)",
        '',
        '| Namespace | Total Imports | Missing from TotK | Sample Missing Symbols |',
        '|---|---:|---:|---|',
    ]
    for area_name in sorted(by_area_total, key=lambda x: (-by_area_missing[x], -by_area_total[x])):
        miss_n = by_area_missing[area_name]
        samples = ', '.join(x['readable'].split(' (')[0] for x in missing_grouped.get(area_name, [])[:3])
        md.append(f'| `{area_name}` | {by_area_total[area_name]} | {miss_n} | {samples} |')

    md += [
        '',
        '## 4. RomFS Summary',
        f"- Total files: **{len(files):,}** | Total size: **{index['romfs_size'] / 1048576:.2f} MiB**",
        f"- NRO/NRR files found: **{len(nros_or_nrrs)}** -> {nros_or_nrrs}",
        f"- Shader files (`.sharcb`/`.bnsh`/`.bahs`/`.bfsha`): **{len(shader_files)}**",
    ]
    for p, sz in shader_files[:20]:
        md.append(f'  - `{p}` ({sz:,} B)')
    md += [
        '',
        '### Top-level folders',
    ]
    for k, cnt in top_counts.most_common():
        md.append(f'- `{k}`: {cnt:,} files ({top_bytes[k] / 1048576:.2f} MiB)')
    md += [
        '',
        '### Top file extensions',
    ]
    for k, cnt in ext_counts.most_common(20):
        md.append(f'- `{k}`: {cnt:,} files ({ext_bytes[k] / 1048576:.2f} MiB)')

    (work / 'survey_v0.md').write_text('\n'.join(md) + '\n', encoding='utf-8')
    print(f'Wrote {work / "survey_v0.json"} and {work / "survey_v0.md"}')


if __name__ == '__main__':
    main()

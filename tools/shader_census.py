"""Census of every Maxwell shader in the extracted Nintendo Switch Sports RomFS and ExeFS, and offline translation.

Adapted from totk-pc-recomp/tools/shader_census.py (MIT).

Reads build/extracted/<version>/ (RomFS image + index.json, and exefs/). Writes under build/<version>/shaders/:
    code/<key>.code     translator input (graphics: SPH + instructions, compute: instructions)
    jobs.json           key -> job, source path, origin (bnsh|sharcb|main)
    spv/<key>.spv       with --translate
    failures.txt        with --translate, grouped failures

Needs Python 3.14 (compression.zstd), so run in .venv-py314:

    .venv-py314\\Scripts\\python tools\\shader_census.py --version v0 --translate
"""
import argparse
import compression.zstd as zstd
import concurrent.futures
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import time
from collections import defaultdict
from pathlib import Path

import lz4.block

ROOT = Path(__file__).resolve().parent.parent
XLATE = ROOT / 'build' / 'xlate' / ('nssports_xlate.exe' if os.name == 'nt' else 'nssports_xlate')
SPIRV_VAL = (ROOT / 'toolchain' / 'msys64' / 'ucrt64' / 'bin' / 'spirv-val.exe') if os.name == 'nt' else Path(
    shutil.which('spirv-val') or '/usr/bin/spirv-val')
DOTNET_ROOT = ROOT / 'toolchain' / 'dotnet'
ZSTD = b'\x28\xb5\x2f\xfd'
CONTROL_MAGIC = b'\x34\x12\x76\x98'
STAGE_IDS = {0: 'vs', 1: 'ps', 5: 'cs'}
MAX_WORKERS = 16


def digest(data):
    return hashlib.sha256(data).hexdigest()


def checked(data, offset, size):
    if offset < 0 or size < 0 or offset + size > len(data):
        raise ValueError('shader container range outside file')
    return data[offset:offset + size]


def sarc(data):
    if data[:4] != b'SARC':
        raise ValueError('expected SARC')
    endian = '<' if data[6:8] == b'\xff\xfe' else '>'
    header, size, start = struct.unpack_from(endian + 'HxxII', data, 4)
    if size != len(data) or header < 20 or checked(data, header, 4) != b'SFAT':
        raise ValueError('invalid SARC header')
    count = struct.unpack_from(endian + 'H', data, header + 6)[0]
    names = header + 12 + count * 16
    if checked(data, names, 4) != b'SFNT':
        raise ValueError('invalid SARC string table')
    names += 8
    result = {}
    for i in range(count):
        _, attribute, lo, hi = struct.unpack_from(endian + '4I', checked(data, header + 12 + i * 16, 16))
        off = names + (attribute & 0xffffff) * 4
        if attribute >> 24 != 1 or not names <= off < start or hi < lo or start + lo < start:
            raise ValueError('invalid SARC member')
        name = data[off:data.index(0, off, start)].decode('utf-8')
        if name.startswith('/') or '\\' in name or any(p in ('', '.', '..') for p in name.split('/')) or name in result:
            raise ValueError('unsafe or duplicate SARC name')
        result[name] = checked(data, start + lo, hi - lo)
    return result


def bnsh_shaders(data):
    """BNSH absolute offsets are relative to each embedded BNSH, not BFSHA."""
    if data[:4] != b'BNSH' or checked(data, 12, 2) != b'\xff\xfe' or checked(data, 96, 4) != b'grsc':
        raise ValueError('unsupported BNSH header')
    size = struct.unpack_from('<I', data, 28)[0]
    data = checked(data, 0, size)
    count, variations = struct.unpack_from('<IQ', data, 124)
    checked(data, variations, count * 64)
    for i in range(count):
        program = struct.unpack_from('<Q', data, variations + i * 64 + 16)[0]
        checked(data, program, 56)
        for stage, relative in [('vs', 8), ('ps', 40), ('cs', 48)]:
            offset = struct.unpack_from('<Q', data, program + relative)[0]
            if not offset:
                continue
            control_off, code_off, code_size, control_size = struct.unpack_from('<QQII', checked(data, offset + 8, 24))
            control = checked(data, control_off, control_size)
            code = checked(data, code_off, code_size)
            if len(control) < 0x880:
                continue
            control = control[:0x880]
            if control[:4] != CONTROL_MAGIC:
                raise ValueError('NVN control section without its magic')
            c1, length = struct.unpack_from('<II', control, 0x700)
            actual = struct.unpack_from('<I', control, 0x714)[0]
            if actual != {'vs': 0, 'ps': 1, 'cs': 5}[stage] or not 0 <= c1 <= length <= len(code) or not length:
                raise ValueError('invalid NVN shader bounds or stage')
            yield digest(control + code[:length]), stage, control, code[:length]


def sharcb_shaders(data, rejected):
    """agl shader packages (.sharcb, magic BAHS)."""
    if len(data) < 32 or data[:4] != b'BAHS':
        raise ValueError('expected an agl shader package')
    start, pool_size, pool = struct.unpack_from('<3I', data, 20)
    if not 32 <= start <= pool or pool + pool_size > len(data):
        raise ValueError('agl shader package layout outside the file')
    seen = set()
    for size in (b'\x80\x08\x00\x00', b'\x8c\x08\x00\x00'):
        at = start
        while (at := data.find(size, at, pool)) >= 0:
            record, at = at - 8, at + 4
            if record < start or record % 4:
                continue
            code_off = struct.unpack_from('<Q', data, record)[0]
            control_at = record + struct.unpack_from('<i', data, record + 12)[0]
            if not start <= control_at <= pool - 0x880 or code_off >= pool_size:
                continue
            control = data[control_at:control_at + 0x880]
            if control[:4] != CONTROL_MAGIC:
                rejected[0] += 1
                continue
            c1, length = struct.unpack_from('<II', control, 0x700)
            stage = STAGE_IDS.get(struct.unpack_from('<I', control, 0x714)[0])
            if not stage or not 0 < length <= pool_size - code_off or c1 > length:
                continue
            code = data[pool + code_off:pool + code_off + length]
            key = digest(control + code)
            if key not in seen:
                seen.add(key)
                yield key, stage, control, code


def nso_image(path):
    raw = path.read_bytes()
    if raw[:4] != b'NSO0':
        raise ValueError(f'{path} is not an NSO')
    flags = struct.unpack_from('<I', raw, 0xC)[0]
    parts = []
    for i in range(3):
        file_off, _, dec_size = struct.unpack_from('<III', raw, 0x10 + i * 0x10)
        comp_size = struct.unpack_from('<I', raw, 0x60 + i * 4)[0]
        data = raw[file_off:file_off + comp_size]
        parts.append(lz4.block.decompress(data, uncompressed_size=dec_size) if flags & (1 << i) else data)
    return b''.join(parts)


def startup_package(name):
    return (name == 'Lib/NintendoSDK/TextureShader.bnsh' or
            name.startswith('Shader/') or
            name.startswith('Lib/agl/') or
            name.startswith('Lib/gsys/') or
            name.endswith('.sharcb') or
            name.endswith('.bnsh'))


def inventory(romfs, exefs):
    index = json.loads((romfs / 'index.json').read_text(encoding='utf-8'))
    if index.get('status') != 'pass':
        raise ValueError('RomFS has not passed integrity verification')
    entries = {r['path']: r for r in index['files']}
    shaders = {}
    rejected = [0]

    def add(origin, name, found, startup):
        for key, stage, control, code in found:
            s = shaders.setdefault(key, dict(stage=stage, control=control, code=code, source=name, origin=origin,
                                             startup=startup))
            if startup and not s['startup']:
                s.update(source=name, origin=origin, startup=True)

    with (romfs / 'romfs.bin').open('rb') as stream:
        def read(name):
            row = entries[name]
            stream.seek(row['offset'])
            raw = stream.read(row['size'])
            if len(raw) != row['size']:
                raise ValueError('short RomFS read')
            return raw

        dictionaries = {}
        for zsdic_name in ('Pack/ZsDic.pack.zs',):
            if zsdic_name in entries:
                for raw in sarc(zstd.decompress(read(zsdic_name))).values():
                    d = zstd.ZstdDict(raw)
                    dictionaries[d.dict_id] = d

        def decode(raw):
            info = zstd.get_frame_info(raw)
            if info.decompressed_size is not None and info.decompressed_size > 512 << 20:
                raise ValueError('unbounded shader package')
            out = zstd.decompress(raw, zstd_dict=dictionaries.get(info.dictionary_id) if info.dictionary_id else None)
            if len(out) > 512 << 20:
                raise ValueError('unbounded shader package')
            return out

        def visit(origin, name, data, startup, depth=0):
            if data[:4] == b'BAHS':
                add('sharcb', name, sharcb_shaders(data, rejected), startup)
                return
            if data[:4] == b'SARC':
                if depth > 4:
                    return
                for member, raw in sarc(data).items():
                    visit(origin, name + '/' + member, decode(raw) if raw[:4] == ZSTD else raw, startup, depth + 1)
                return
            pos = 0
            while (pos := data.find(b'BNSH\0\0\0\0', pos)) >= 0:
                if pos + 32 > len(data):
                    break
                size = struct.unpack_from('<I', data, pos + 28)[0]
                if size < 136 or pos + size > len(data):
                    pos += 8
                    continue
                try:
                    add(origin, name, bnsh_shaders(data[pos:pos + size]), startup)
                except ValueError:
                    pass
                pos += max(size, 8)

        for name in sorted(entries):
            raw = read(name)
            if raw[:4] not in (ZSTD, b'SARC', b'FSHA', b'BNSH', b'BAHS') and b'BNSH\0\0\0\0' not in raw:
                continue
            try:
                dec = decode(raw) if raw[:4] == ZSTD else raw
            except Exception:
                continue
            if dec[:4] in (b'SARC', b'FSHA', b'BNSH', b'BAHS') or b'BNSH\0\0\0\0' in dec:
                visit('bnsh', name, dec, startup_package(name))
    for module in sorted(exefs.iterdir()):
        if module.name in ('main', 'sdk', 'subsdk0', 'rtld'):
            visit('main', 'module:' + module.name, nso_image(module), True)
    return shaders, rejected[0]


def write_shaders(shaders, out, everything):
    (out / 'code').mkdir(parents=True, exist_ok=True)
    jobs = {}
    for key, s in sorted(shaders.items()):
        if not s['startup'] and not everything:
            continue
        control, code, stage = s['control'], s['code'], s['stage']
        c1, length = struct.unpack_from('<II', control, 0x700)
        body = code[0 if stage == 'cs' else 0x30:length]
        path = out / 'code' / f'{key}.code'
        if not path.is_file() or path.read_bytes() != body:
            path.write_bytes(body)
        job = dict(key=key, stage=stage, code=str(path), c1=code[c1:length].hex(), debug=False)
        if stage == 'cs':
            job['local'] = list(struct.unpack_from('<III', control, 0x748))
        jobs[key] = dict(job=job, source=s['source'], origin=s['origin'], startup=s['startup'])
    (out / 'jobs.json').write_text(json.dumps(jobs, indent=0), encoding='utf-8')
    return jobs


def worker(keys, jobs, spv_dir, env):
    failures, errors, ok = {}, {}, 0
    process = subprocess.Popen([str(XLATE), '--serve'], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.DEVNULL, text=True, env=env)
    try:
        for key in keys:
            process.stdin.write(json.dumps({'job': jobs[key]['job'], 'out': str(spv_dir)}) + '\n')
            process.stdin.flush()
            reply = process.stdout.readline().rstrip('\n')
            if not reply:
                failures[key] = 'translator service exited'
                break
            if not reply.startswith('ok '):
                failures[key] = reply.split(' ', 2)[2] if reply.count(' ') >= 2 else reply
                continue
            v = subprocess.run([str(SPIRV_VAL), '--target-env', 'vulkan1.3', str(spv_dir / f'{key}.spv')],
                               capture_output=True, text=True)
            if v.returncode:
                text = (v.stderr or v.stdout).strip()
                errors[key] = text.splitlines()[0] if text else 'spirv-val failed'
            else:
                ok += 1
    finally:
        process.stdin.close()
        process.wait()
    return ok, failures, errors


def normalize(message):
    return re.sub(r'\b\d+\b|0x[0-9a-fA-F]+', 'N', message)[:300]


def translate(jobs, out):
    spv_dir = out / 'spv'
    spv_dir.mkdir(exist_ok=True)
    env = dict(os.environ)
    if DOTNET_ROOT.is_dir() and 'DOTNET_ROOT' not in env:
        env['DOTNET_ROOT'] = str(DOTNET_ROOT)
    keys = sorted(jobs)
    count = max(1, min(MAX_WORKERS, len(keys)))
    slices = [keys[i::count] for i in range(count)]
    ok, failures, errors = 0, {}, {}
    with concurrent.futures.ThreadPoolExecutor(count) as pool:
        for n, f, e in pool.map(lambda s: worker(s, jobs, spv_dir, env), slices):
            ok += n
            failures.update(f)
            errors.update(e)
    if failures or errors:
        lines = []
        for title, table in (('translate failures', failures), ('spirv-val errors', errors)):
            groups = defaultdict(list)
            for key, message in table.items():
                groups[normalize(message)].append((key, message))
            lines.append(f'== {title}: {len(table)} in {len(groups)} groups')
            for _, members in sorted(groups.items(), key=lambda g: -len(g[1])):
                lines.append(f'{len(members)}\t{members[0][1][:300]}')
                lines.extend(f'\texample {key}' for key, _ in sorted(members)[:2])
        (out / 'failures.txt').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    else:
        (out / 'failures.txt').unlink(missing_ok=True)
    return ok, len(failures), len(errors)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--version', default='v0', help='version dir (default v0 -> build/extracted/v0)')
    ap.add_argument('--all', action='store_true', default=True, help='include all shaders in RomFS (default True)')
    ap.add_argument('--startup-only', action='store_true', help='only startup shader packs')
    ap.add_argument('--translate', action='store_true', help='translate every shader and run spirv-val')
    args = ap.parse_args()
    t0 = time.monotonic()
    extracted = ROOT / 'build' / 'extracted' / args.version
    out = ROOT / 'build' / args.version / 'shaders'
    out.mkdir(parents=True, exist_ok=True)
    shaders, rejected = inventory(extracted / 'romfs', extracted / 'exefs')
    everything = not args.startup_only
    jobs = write_shaders(shaders, out, everything)
    a, b, c = (sum(1 for j in jobs.values() if j['origin'] == o) for o in ('bnsh', 'sharcb', 'main'))
    print(f'{len(shaders)} unique shaders found in all, {len(jobs)} selected; {rejected} .sharcb record candidates '
          f'without a control section rejected', flush=True)
    summary = f'shaders {len(jobs)} (bnsh {a}, sharcb {b}, main {c})'
    print(f'extracted {summary} in {time.monotonic() - t0:.1f} s', flush=True)
    if not args.translate:
        print(f'{summary}, total {time.monotonic() - t0:.1f} s')
        return 0
    if not XLATE.is_file() or not SPIRV_VAL.is_file():
        sys.exit(f'missing {XLATE} or {SPIRV_VAL}')
    ok, failed, bad = translate(jobs, out)
    print(f'{summary}, translated {ok}, failed {failed}, spirv-val errors {bad}, total {time.monotonic() - t0:.1f} s')
    return 0


if __name__ == '__main__':
    sys.exit(main())

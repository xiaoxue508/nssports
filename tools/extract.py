"""Extract Nintendo Switch Sports (0100D2F00D5C0000) ExeFS and RomFS from the configured NSP/XCI.

Uses the verified decryption and IVFC-checking primitives from tools/vendor/macnativegames/ without hardcoding
TotK's Title ID. Key values are read in-memory from the keys file and never printed.

Run under .venv-py314:
    .venv-py314\\Scripts\\python.exe tools\\extract.py
"""
import argparse
import bisect
import hashlib
import io
import json
import shutil
import struct
import sys
import tomllib
from pathlib import Path

from cryptography.hazmat.primitives.ciphers import modes

ROOT = Path(__file__).resolve().parent.parent
VENDOR = ROOT / 'tools' / 'vendor' / 'macnativegames'
sys.path.insert(0, str(VENDOR))

from extract_exefs import (  # noqa: E402
    common_tickets,
    container_entries,
    decrypt,
    load_keys,
    nca_ctr_key,
    nca_inventory,
    partition_entries,
    read_at,
    record_offset,
    require,
)
from extract_romfs import index_romfs  # noqa: E402

TITLE_ID = '0100D2F00D5C0000'


def select_base_program(records):
    """Find the single base Program NCA for TITLE_ID (0100D2F00D5C0000)."""
    metadata = [r for r, _ in records if r['content_type'] == 1]
    require(len(metadata) <= 1 and all(r['title_id'] == TITLE_ID for r in metadata),
            f'Expected base game metadata for {TITLE_ID}; found {[r["title_id"] for r in metadata]}')
    programs = [(r, h) for r, h in records if r['content_type'] == 0]
    require(len(programs) == 1, f'Expected one base Program NCA, found {len(programs)}')
    record, hdr = programs[0]
    require(record['title_id'] == TITLE_ID, f'Expected Title ID {TITLE_ID}, got {record["title_id"]}')
    return record, hdr


def extract_exefs(source: Path, keys: dict, out_dir: Path, *, allow_update: bool = False):
    exefs_dir = out_dir / 'exefs'
    manifest_path = out_dir / 'extraction-manifest.json'
    if (exefs_dir / 'main').is_file() and (exefs_dir / 'main.npdm').is_file() and manifest_path.is_file():
        print(f'ExeFS already extracted in {exefs_dir}')
        return

    if exefs_dir.exists():
        shutil.rmtree(exefs_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    with source.open('rb') as f:
        kind, entries = container_entries(f, source.stat().st_size)
        tickets = common_tickets(f, entries)
        records = nca_inventory(f, entries, keys)
        if allow_update:
            programs = [(r, h) for r, h in records if r['content_type'] == 0]
            require(len(programs) == 1, f'Expected one Program NCA, found {len(programs)}')
            record, hdr = programs[0]
            require(record['title_id'] == TITLE_ID, f'Expected Title ID {TITLE_ID}, got {record["title_id"]}')
        else:
            record, hdr = select_base_program(records)
        ctr_key = nca_ctr_key(hdr, keys, tickets)

        for i in range(4):
            start, end = struct.unpack_from('<II', hdr, 0x240 + 16 * i)
            if not start:
                continue
            fs = hdr[0x400 + 512 * i:0x600 + 512 * i]
            require(hashlib.sha256(fs).digest() == hdr[0x280 + 32 * i:0x2A0 + 32 * i], 'FS header hash mismatch')
            if fs[2:4] != b'\x01\x02':
                continue
            require(fs[4] == 3, 'Only AES-CTR ExeFS is supported')
            blocksize, levels = struct.unpack_from('<II', fs, 0x28)
            hashoff, hashsize, pfsoff, pfssize = struct.unpack_from('<QQQQ', fs, 0x30)
            secstart, secsize = start * 512, (end - start) * 512
            require(levels == 2 and blocksize > 0 and pfssize <= 512 * 1024 * 1024, 'Unsupported ExeFS hash layout')

            def section_read(relative, size):
                require(0 <= relative and relative + size <= secsize, 'Section read out of bounds')
                absolute = secstart + relative
                aligned = absolute & ~15
                iv = fs[0x140:0x148][::-1] + (aligned // 16).to_bytes(8, 'big')
                data = read_at(f, record_offset(record) + aligned, size + absolute - aligned)
                return decrypt(ctr_key, modes.CTR(iv), data)[absolute - aligned:]

            hashes = section_read(hashoff, hashsize)
            require(hashlib.sha256(hashes).digest() == fs[8:40], 'ExeFS hash-table mismatch')
            pfs = section_read(pfsoff, pfssize)
            for index, pos in enumerate(range(0, pfssize, blocksize)):
                expected = hashes[index * 32:(index + 1) * 32]
                require(hashlib.sha256(pfs[pos:pos + blocksize]).digest() == expected,
                        f'ExeFS block hash mismatch: {index}')
            files = [(name, pfs[off:off + size]) for name, off, size in
                     partition_entries(io.BytesIO(pfs), 0, len(pfs))]
            require(any(name == 'main.npdm' for name, _ in files), 'PFS0 is not ExeFS')

            exefs_dir.mkdir(parents=True)
            extracted = []
            for name, data in files:
                (exefs_dir / name).write_bytes(data)
                extracted.append({'name': name, 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()})

            manifest = {
                'source': str(source.resolve()),
                'source_size': source.stat().st_size,
                'source_mtime_ns': source.stat().st_mtime_ns,
                'container': kind,
                'nca_inventory': [r for r, _ in records],
                'program': record,
                'section_index': i,
                'integrity': 'FS header, ExeFS hash table, and every ExeFS data block verified; RSA signatures not checked',
                'files': extracted,
            }
            manifest_path.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
            print(f'Extracted ExeFS ({len(extracted)} files) to {exefs_dir}')
            return
    raise ValueError('No ExeFS section found')


def extract_romfs(source: Path, keys: dict, out_dir: Path, *, unpack_files: bool = False):
    romfs_dir = out_dir / 'romfs'
    index_path = romfs_dir / 'index.json'
    image_path = romfs_dir / 'romfs.bin'
    manifest = json.loads((out_dir / 'extraction-manifest.json').read_text(encoding='utf-8'))

    if index_path.is_file() and image_path.is_file():
        report = json.loads(index_path.read_text(encoding='utf-8'))
        if report.get('romfs_size') and report.get('romfs_sha256') and image_path.stat().st_size == report['romfs_size']:
            print(f'RomFS already extracted & verified in {romfs_dir} ({len(report["files"]):,} files, {report["romfs_size"]:,} bytes)')
            return

    if romfs_dir.exists():
        shutil.rmtree(romfs_dir)

    record = manifest['program']
    require(source.stat().st_size == manifest['source_size'], 'Source size changed')

    with source.open('rb') as f:
        _, entries = container_entries(f, source.stat().st_size)
        tickets = common_tickets(f, entries)
        actual, hdr = select_base_program(nca_inventory(f, entries, keys))
        require(actual['name'] == record['name'] and actual['size'] == record['size'] and
                record_offset(actual) == record_offset(record),
                'Source Program NCA changed; extract ExeFS again')
        ctr_key = nca_ctr_key(hdr, keys, tickets)

        for i in range(4):
            start, end = struct.unpack_from('<II', hdr, 0x240 + i * 16)
            if not start:
                continue
            fs = hdr[0x400 + 512 * i:0x600 + 512 * i]
            require(hashlib.sha256(fs).digest() == hdr[0x280 + 32 * i:0x2a0 + 32 * i], 'FS header hash')
            if fs[2:5] != bytes([0, 3, 3]):
                continue
            require(fs[8:12] == b'IVFC' and struct.unpack_from('<III', fs, 12) == (0x20000, 32, 7),
                    'Unsupported IVFC layout')
            levels = [struct.unpack_from('<QQII', fs, 0x18 + j * 24) for j in range(6)]

            def read(relative, size):
                require(0 <= relative <= relative + size <= (end - start) * 512, 'Section bounds')
                absolute = start * 512 + relative
                aligned = absolute & ~15
                skip = absolute - aligned
                iv = fs[0x140:0x148][::-1] + (aligned // 16).to_bytes(8, 'big')
                return decrypt(ctr_key, modes.CTR(iv), read_at(f, record_offset(record) + aligned, size + skip))[skip:]

            romfs_dir.mkdir(parents=True)
            digest = hashlib.sha256()
            counts = []
            parent = fs[0xc8:0xe8]
            with image_path.open('wb') as image:
                for level, (offset, size, power, reserved) in enumerate(levels):
                    require(12 <= power <= 24 and reserved == 0, 'IVFC block size')
                    block = 1 << power
                    require(len(parent) >= (size + block - 1) // block * 32, 'IVFC parent size')
                    captured = bytearray() if level < 5 else None
                    index = 0
                    for pos in range(0, size, 8 * 1024 * 1024):
                        chunk = read(offset + pos, min(8 * 1024 * 1024, size - pos))
                        for j in range(0, len(chunk), block):
                            payload = chunk[j:j + block]
                            require(hashlib.sha256(payload + bytes(block - len(payload))).digest() ==
                                    parent[index * 32:index * 32 + 32],
                                    f'IVFC level {level} block {index} failed')
                            index += 1
                        if captured is not None:
                            captured.extend(chunk)
                        else:
                            image.write(chunk)
                            digest.update(chunk)
                    counts.append(index)
                    if captured is not None:
                        parent = bytes(captured)
                    print(f'IVFC level {level}: {index} blocks verified', flush=True)

            files = index_romfs(image_path)
            report = {
                'status': 'pass',
                'source_size': source.stat().st_size,
                'program': record['name'],
                'section': i,
                'ivfc_blocks': counts,
                'romfs_size': levels[-1][1],
                'romfs_sha256': digest.hexdigest(),
                'files': files,
                'integrity': 'FS header and all six IVFC levels verified; RSA signature not checked',
            }
            index_path.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
            print(f'Verified RomFS: {len(files):,} files, {levels[-1][1]:,} bytes, sha256={digest.hexdigest()[:16]}...')

            if unpack_files:
                files_dir = romfs_dir / 'files'
                with image_path.open('rb') as img:
                    for entry in files:
                        dst = files_dir / entry['path']
                        dst.parent.mkdir(parents=True, exist_ok=True)
                        img.seek(entry['offset'])
                        dst.write_bytes(img.read(entry['size']))
                print(f'Unpacked {len(files):,} RomFS files to {files_dir}')
            return
    raise ValueError('No supported RomFS found')


def _parse_bktr_table(blob: bytes, entry_fmt: str):
    """Parse a Switch BKTR relocation table (Indirect or AesCtrEx) into sorted (virt_offsets, entries)."""
    version, num_buckets, total_size = struct.unpack_from('<IIQ', blob, 0)
    require(version == 0, f'Unsupported BKTR table version {version}')
    entry_size = struct.calcsize(entry_fmt)
    offs = []
    entries = []
    pos = 0x4000  # BKTR buckets start at 0x4000 (16 KiB header: 16B header + up to 16368B bucket offsets)
    # Header buckets array starts at 0x10; each bucket is 16 KiB (0x4000) in the table data.
    for b in range(num_buckets):
        b_off = 0x4000 + b * 0x4000
        _, n_ent, _ = struct.unpack_from('<IIQ', blob, b_off)
        epos = b_off + 16
        for _ in range(n_ent):
            fields = struct.unpack_from(entry_fmt, blob, epos)
            epos += entry_size
            offs.append(fields[0])
            entries.append(fields)
    require(len(offs) > 0, 'Empty BKTR table')
    return offs, entries, total_size


def extract_update_romfs(base_source: Path, update_source: Path, keys: dict, out_dir: Path, *, unpack_files: bool = False):
    romfs_dir = out_dir / 'romfs'
    index_path = romfs_dir / 'index.json'
    image_path = romfs_dir / 'romfs.bin'
    if index_path.is_file() and image_path.is_file():
        report = json.loads(index_path.read_text(encoding='utf-8'))
        if report.get('romfs_size') and report.get('romfs_sha256') and image_path.stat().st_size == report['romfs_size']:
            print(f'RomFS already extracted & verified in {romfs_dir} ({len(report["files"]):,} files, {report["romfs_size"]:,} bytes)')
            return

    if romfs_dir.exists():
        shutil.rmtree(romfs_dir)

    with base_source.open('rb') as bf, update_source.open('rb') as uf:
        _, b_entries = container_entries(bf, base_source.stat().st_size)
        b_tickets = common_tickets(bf, b_entries)
        b_rec, b_hdr = select_base_program(nca_inventory(bf, b_entries, keys))
        b_key = nca_ctr_key(b_hdr, keys, b_tickets)

        _, u_entries = container_entries(uf, update_source.stat().st_size)
        u_tickets = common_tickets(uf, u_entries)
        u_progs = [(r, h) for r, h in nca_inventory(uf, u_entries, keys) if r['content_type'] == 0]
        require(len(u_progs) == 1, 'Expected one Program NCA in update')
        u_rec, u_hdr = u_progs[0]
        u_key = nca_ctr_key(u_hdr, keys, u_tickets)

        b_sec = None
        for i in range(4):
            st, en = struct.unpack_from('<II', b_hdr, 0x240 + i * 16)
            if st and b_hdr[0x400 + 512 * i + 2:0x400 + 512 * i + 5] == bytes([0, 3, 3]):
                b_sec = (st, en, b_hdr[0x400 + 512 * i:0x600 + 512 * i])
                break
        require(b_sec is not None, 'Base RomFS section not found')

        u_sec = None
        u_sec_idx = -1
        for i in range(4):
            st, en = struct.unpack_from('<II', u_hdr, 0x240 + i * 16)
            if st and u_hdr[0x400 + 512 * i + 4] == 4:
                u_sec = (st, en, u_hdr[0x400 + 512 * i:0x600 + 512 * i])
                u_sec_idx = i
                break
        require(u_sec is not None, 'Update BKTR RomFS section not found')

        b_st, b_en, b_fs = b_sec
        u_st, u_en, u_fs = u_sec
        b_iv_base = b_fs[0x140:0x148][::-1]
        u_iv_sec = u_fs[0x144:0x148][::-1]
        u_iv_base = u_fs[0x140:0x148][::-1]

        def read_base(rel, size):
            abs_off = b_st * 512 + rel
            al = abs_off & ~15
            sk = abs_off - al
            iv = b_iv_base + (al // 16).to_bytes(8, 'big')
            return decrypt(b_key, modes.CTR(iv), read_at(bf, record_offset(b_rec) + al, size + sk))[sk:]

        def read_upd_plain(rel, size):
            abs_off = u_st * 512 + rel
            al = abs_off & ~15
            sk = abs_off - al
            iv = u_iv_base + (al // 16).to_bytes(8, 'big')
            return decrypt(u_key, modes.CTR(iv), read_at(uf, record_offset(u_rec) + al, size + sk))[sk:]

        def read_upd_gen(rel, size, gen):
            abs_off = u_st * 512 + rel
            al = abs_off & ~15
            sk = abs_off - al
            iv = u_iv_sec + struct.pack('>I', gen) + (al // 16).to_bytes(8, 'big')
            return decrypt(u_key, modes.CTR(iv), read_at(uf, record_offset(u_rec) + al, size + sk))[sk:]

        b1_off, b1_sz = struct.unpack_from('<QQ', u_fs, 0x100)
        b2_off, b2_sz = struct.unpack_from('<QQ', u_fs, 0x120)
        require(u_fs[0x110:0x114] == b'BKTR' and u_fs[0x130:0x134] == b'BKTR', 'Missing BKTR magic')

        ind_offs, ind_ents, ind_total = _parse_bktr_table(read_upd_plain(b1_off, b1_sz), '<QQI')
        aes_offs, aes_ents, _ = _parse_bktr_table(read_upd_plain(b2_off, b2_sz), '<QII')

        def read_bktr(virt, size):
            out = bytearray()
            cur = virt
            rem = size
            while rem > 0:
                idx = bisect.bisect_right(ind_offs, cur) - 1
                require(idx >= 0, f'BKTR indirect lookup failed at {cur:#x}')
                v0, src_off, is_patch = ind_ents[idx]
                v1 = ind_offs[idx + 1] if idx + 1 < len(ind_offs) else ind_total
                take = min(rem, v1 - cur)
                require(take > 0, f'Zero-length BKTR span at {cur:#x}')
                delta = cur - v0
                if not is_patch:
                    out.extend(read_base(src_off + delta, take))
                else:
                    p_cur = src_off + delta
                    p_rem = take
                    while p_rem > 0:
                        aidx = bisect.bisect_right(aes_offs, p_cur) - 1
                        require(aidx >= 0, f'BKTR AesCtrEx lookup failed at {p_cur:#x}')
                        a0, _, gen = aes_ents[aidx]
                        a1 = aes_offs[aidx + 1] if aidx + 1 < len(aes_offs) else b1_off
                        atake = min(p_rem, a1 - p_cur)
                        require(atake > 0, f'Zero-length AesCtrEx span at {p_cur:#x}')
                        out.extend(read_upd_gen(p_cur, atake, gen))
                        p_cur += atake
                        p_rem -= atake
                cur += take
                rem -= take
            return bytes(out)

        require(u_fs[8:12] == b'IVFC' and struct.unpack_from('<III', u_fs, 12) == (0x20000, 32, 7),
                'Unsupported update IVFC layout')
        levels = [struct.unpack_from('<QQII', u_fs, 0x18 + j * 24) for j in range(6)]

        romfs_dir.mkdir(parents=True)
        digest = hashlib.sha256()
        counts = []
        parent = u_fs[0xc8:0xe8]
        with image_path.open('wb') as image:
            for level, (offset, size, power, reserved) in enumerate(levels):
                require(12 <= power <= 24 and reserved == 0, 'IVFC block size')
                block = 1 << power
                require(len(parent) >= (size + block - 1) // block * 32, 'IVFC parent size')
                captured = bytearray() if level < 5 else None
                index = 0
                for pos in range(0, size, 8 * 1024 * 1024):
                    chunk = read_bktr(offset + pos, min(8 * 1024 * 1024, size - pos))
                    for j in range(0, len(chunk), block):
                        payload = chunk[j:j + block]
                        require(hashlib.sha256(payload + bytes(block - len(payload))).digest() ==
                                parent[index * 32:index * 32 + 32],
                                f'Update IVFC level {level} block {index} failed')
                        index += 1
                    if captured is not None:
                        captured.extend(chunk)
                    else:
                        image.write(chunk)
                        digest.update(chunk)
                counts.append(index)
                if captured is not None:
                    parent = bytes(captured)
                print(f'Update IVFC level {level}: {index} blocks verified', flush=True)

        files = index_romfs(image_path)
        report = {
            'status': 'pass',
            'source_size': update_source.stat().st_size,
            'program': u_rec['name'],
            'section': u_sec_idx,
            'ivfc_blocks': counts,
            'romfs_size': levels[-1][1],
            'romfs_sha256': digest.hexdigest(),
            'files': files,
            'integrity': 'BKTR merge + all six IVFC levels verified; RSA signature not checked',
        }
        index_path.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        print(f'Verified Update RomFS: {len(files):,} files, {levels[-1][1]:,} bytes, sha256={digest.hexdigest()[:16]}...')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, default=ROOT / 'config.local.toml')
    parser.add_argument('--update', type=Path, help='optional update NSP path')
    parser.add_argument('--version', default='v0', help='output version dir under extracted/ (default v0)')
    parser.add_argument('--unpack-romfs', action='store_true', help='also unpack individual files from romfs.bin')
    args = parser.parse_args()

    cfg = tomllib.loads(args.config.read_text(encoding='utf-8'))
    base = Path(cfg['input']['base'])
    if not base.is_absolute():
        base = ROOT / base
    keys_path = Path(cfg['input']['keys'])
    if not keys_path.is_absolute():
        keys_path = ROOT / keys_path
    work = Path(cfg.get('work', {}).get('dir', 'build'))
    if not work.is_absolute():
        work = ROOT / work

    keys = load_keys(keys_path)
    out_dir = work / 'extracted' / args.version
    if args.update:
        extract_exefs(args.update, keys, out_dir, allow_update=True)
        extract_update_romfs(base, args.update, keys, out_dir, unpack_files=args.unpack_romfs)
    else:
        extract_exefs(base, keys, out_dir)
        extract_romfs(base, keys, out_dir, unpack_files=args.unpack_romfs)


if __name__ == '__main__':
    try:
        main()
    except ValueError as err:
        sys.exit(f'error: {err}')

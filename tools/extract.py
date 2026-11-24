"""Extract Nintendo Switch Sports (0100D2F00D5C0000) ExeFS and RomFS from the configured NSP/XCI.

Uses the verified decryption and IVFC-checking primitives from tools/vendor/macnativegames/ without hardcoding
TotK's Title ID. Key values are read in-memory from the keys file and never printed.

Run under .venv-py314:
    .venv-py314\\Scripts\\python.exe tools\\extract.py
"""
import argparse
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


def extract_exefs(source: Path, keys: dict, out_dir: Path):
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, default=ROOT / 'config.local.toml')
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
    out_v0 = work / 'extracted' / 'v0'
    extract_exefs(base, keys, out_v0)
    extract_romfs(base, keys, out_v0, unpack_files=args.unpack_romfs)


if __name__ == '__main__':
    try:
        main()
    except ValueError as err:
        sys.exit(f'error: {err}')

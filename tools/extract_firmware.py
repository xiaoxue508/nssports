"""Extract Switch system shared fonts (nn::pl) and MiiModel archive (nn::mii) from firmware NCAs.

Uses tools/vendor/macnativegames/extract_shared_fonts.py for shared fonts (0100000000000810..0814)
and extracts MiiModel (0100000000000802) RomFS files into build/extracted/firmware/mii/.
Key values stay in memory and are never printed.

Run under .venv-py314:
    .venv-py314\\Scripts\\python.exe tools\\extract_firmware.py
"""
import argparse
import hashlib
import json
import struct
import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VENDOR = ROOT / 'tools' / 'vendor' / 'macnativegames'
sys.path.insert(0, str(VENDOR))

import extract_shared_fonts  # noqa: E402
from extract_exefs import load_keys, read_at, require  # noqa: E402
from extract_romfs import index_romfs  # noqa: E402

MII_MODEL_TITLE_ID = '0100000000000802'


def find_data_nca(firmware_dir: Path, keys: dict, target_title_id: str) -> Path:
    require(firmware_dir.is_dir(), f'Firmware directory does not exist: {firmware_dir}')
    for path in sorted(firmware_dir.iterdir()):
        if path.is_dir():
            path = path / '00'
        if not path.is_file() or (path.name != '00' and path.suffix.lower() != '.nca'):
            continue
        if path.name.endswith('.cnmt.nca'):
            continue
        with path.open('rb') as f:
            hdr = extract_shared_fonts.nca_header(f, keys)
        if hdr[0x200:0x204] != b'NCA3':
            continue
        title = f'{struct.unpack_from("<Q", hdr, 0x210)[0]:016x}'
        if hdr[0x205] in (4, 5) and title == target_title_id:
            return path
    raise ValueError(f'Firmware directory {firmware_dir} is missing data NCA {target_title_id}')


def extract_mii_model(firmware_dir: Path, keys_path: Path, out_dir: Path):
    manifest_path = out_dir / 'manifest.json'
    if manifest_path.is_file():
        print(f'MiiModel archive already extracted in {out_dir}')
        return
    keys = load_keys(keys_path)
    nca_path = find_data_nca(firmware_dir, keys, MII_MODEL_TITLE_ID)
    out_dir.mkdir(parents=True, exist_ok=True)
    image_path = out_dir / f'{MII_MODEL_TITLE_ID}.romfs'
    extract_shared_fonts.nca_romfs(nca_path, keys, image_path)
    entries = index_romfs(image_path)
    files_report = []
    with image_path.open('rb') as img:
        for e in entries:
            data = read_at(img, e['offset'], e['size'])
            dst = out_dir / e['path']
            dst.parent.mkdir(parents=True, exist_ok=True)
            dst.write_bytes(data)
            files_report.append({
                'path': e['path'],
                'size': e['size'],
                'sha256': hashlib.sha256(data).hexdigest(),
            })
    image_path.unlink()
    manifest = {
        'status': 'pass',
        'title_id': MII_MODEL_TITLE_ID,
        'source_nca': nca_path.name,
        'integrity': 'NCA FS header and six IVFC levels verified; RSA not checked',
        'files': files_report,
    }
    manifest_path.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print(f'Extracted MiiModel ({len(files_report)} files) to {out_dir}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, default=ROOT / 'config.local.toml')
    args = parser.parse_args()

    cfg = tomllib.loads(args.config.read_text(encoding='utf-8'))
    firmware = Path(cfg['input']['firmware'])
    keys_path = Path(cfg['input']['keys'])
    if not keys_path.is_absolute():
        keys_path = ROOT / keys_path
    work = Path(cfg.get('work', {}).get('dir', 'build'))
    if not work.is_absolute():
        work = ROOT / work

    fw_out = work / 'extracted' / 'firmware'
    fonts_out = fw_out / 'shared-fonts'
    if (fonts_out / 'manifest.json').is_file():
        print(f'Shared fonts already extracted in {fonts_out}')
    else:
        fonts_out.parent.mkdir(parents=True, exist_ok=True)
        sys.argv = [
            'extract_shared_fonts.py',
            '--keys', str(keys_path),
            '--firmware', str(firmware),
            '--output', str(fonts_out),
        ]
        extract_shared_fonts.main()

    extract_mii_model(firmware, keys_path, fw_out / 'mii')


if __name__ == '__main__':
    try:
        main()
    except ValueError as err:
        sys.exit(f'error: {err}')

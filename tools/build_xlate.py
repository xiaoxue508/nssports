"""Build the shader translator: build/xlate/nssports_xlate.exe (framework-dependent .NET 10, no MSVC needed).

Copies tools/vendor/ryujinx to build/xlate/ryujinx, applies tools/xlate/patches/*.patch there, then runs
`dotnet build -c Release` using the portable .NET 10 SDK in toolchain/dotnet.

Run with .venv:
    .venv\\Scripts\\python.exe tools\\build_xlate.py
"""
import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VENDOR = ROOT / 'tools' / 'vendor' / 'ryujinx'
XLATE = ROOT / 'tools' / 'xlate'
BUILD = ROOT / 'build' / 'xlate'
DOTNET_DIR = ROOT / 'toolchain' / 'dotnet'


def tree_hashes(base: Path) -> dict[str, str]:
    def build_output(rel: Path) -> bool:
        return len(rel.parts) > 2 and rel.parts[0] == 'src' and rel.parts[2] in ('obj', 'bin')

    return {
        p.relative_to(base).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
        for p in sorted(base.rglob('*'))
        if p.is_file() and not build_output(p.relative_to(base))
    }


def prepare_sources() -> Path:
    src = BUILD / 'ryujinx'
    if src.exists():
        shutil.rmtree(src, ignore_errors=True)
    BUILD.mkdir(parents=True, exist_ok=True)
    shutil.copytree(VENDOR, src)
    for junk in list(src.glob('src/*/obj')) + list(src.glob('src/*/bin')):
        shutil.rmtree(junk)
    patches = sorted((XLATE / 'patches').glob('*.patch'))
    for patch in patches:
        for extra in (['--check'], []):
            r = subprocess.run(
                ['git', 'apply', *extra, '--whitespace=nowarn', '--directory', str(src.relative_to(BUILD)), str(patch)],
                cwd=BUILD, capture_output=True, text=True,
            )
            if r.returncode != 0:
                sys.exit(f'patch does not apply: {patch.name}\n{r.stdout}{r.stderr}')
        print(f'applied {patch.name}')
    return src


def identity() -> str:
    h = hashlib.sha256()
    files = [Path(__file__).resolve(), XLATE / 'Program.cs', XLATE / 'nssports_xlate.csproj',
             *sorted((XLATE / 'patches').glob('*.patch'))]
    for f in files:
        h.update(f'{f.name}\0{hashlib.sha256(f.read_bytes()).hexdigest()}\n'.encode())
    for rel, digest in tree_hashes(VENDOR).items():
        h.update(f'vendor/{rel}\0{digest}\n'.encode())
    return h.hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--out', type=Path, help='build directory instead of build/xlate')
    a = parser.parse_args()
    global BUILD
    if a.out:
        BUILD = a.out.resolve()

    started = time.time()
    dotnet = DOTNET_DIR / ('dotnet.exe' if os.name == 'nt' else 'dotnet')
    if not dotnet.is_file():
        found = shutil.which('dotnet')
        if not found:
            sys.exit(f'missing: {dotnet}')
        dotnet = Path(found)

    env = dict(os.environ)
    env['DOTNET_ROOT'] = str(DOTNET_DIR)
    env['DOTNET_CLI_HOME'] = str(ROOT / 'toolchain' / 'dotnet-home')
    env['NUGET_PACKAGES'] = str(ROOT / 'toolchain' / 'nuget-cache')
    env['TEMP'] = str(ROOT / 'toolchain' / 'tmp')
    env['TMP'] = str(ROOT / 'toolchain' / 'tmp')

    src = prepare_sources()
    work = BUILD / 'xlate'
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    for name in ('Program.cs', 'nssports_xlate.csproj'):
        shutil.copy2(XLATE / name, work / name)

    out = BUILD / 'bin'
    shutil.rmtree(out, ignore_errors=True)
    cmd = [
        str(dotnet), 'build', str(work / 'nssports_xlate.csproj'),
        '-c', 'Release',
        '-p:InvariantGlobalization=true',
        f'-p:RyuSrc={src}',
        '-o', str(out),
    ]
    if subprocess.run(cmd, env=env).returncode != 0:
        sys.exit('dotnet build failed')

    for f in out.iterdir():
        if f.is_file():
            shutil.copy2(f, BUILD / f.name)

    ident = identity()
    (BUILD / 'nssports_xlate.id').write_text(ident + '\n', encoding='ascii')
    exe = BUILD / ('nssports_xlate.exe' if os.name == 'nt' else 'nssports_xlate')
    print(f'built {exe} (id {ident[:16]}) in {time.time() - started:.1f} s')


if __name__ == '__main__':
    main()

"""Refuse files that look like game content, keys or generated game code.

Adapted from totk-pc-recomp tools/check_content.py (MIT).
With no arguments it checks every tracked/unignored file; with --staged it checks staged files only.
Standard library only.
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

MAX_BYTES = 1024 * 1024

BLOCKED_SUFFIXES = {
    ".xci", ".xcz", ".nsp", ".nsz", ".nca", ".ncz", ".tik", ".cert",
    ".nso", ".nro", ".nrr", ".npdm", ".keys",
    ".bfres", ".bntx", ".bnsh", ".bahs", ".bfsha", ".sarc", ".szs", ".zs",
    ".pack", ".byml", ".bgyml", ".msbt", ".bars", ".bwav", ".bfwav",
    ".bfstm", ".bfttf", ".bfotf", ".sharcb", ".webm",
}

BLOCKED_NAMES = {"main", "sdk", "rtld", "subsdk0", "subsdk1", "subsdk2", "main.npdm"}

GENERATED_MARKER = "NSS_RECOMP_" + "GENERATED_FROM_GAME_CODE"

KEY_LINE = re.compile(
    r"^\s*[A-Za-z0-9_]*(key|kek|seed)[A-Za-z0-9_]*\s*=\s*[0-9A-Fa-f]{32,}\s*$",
    re.MULTILINE,
)


def list_files(staged: bool) -> list[Path]:
    if staged:
        cmd = ["git", "diff", "--cached", "--name-only", "--diff-filter=ACMR", "-z"]
    else:
        cmd = ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"]
    out = subprocess.run(cmd, check=True, capture_output=True).stdout
    return [Path(p) for p in out.decode("utf-8").split("\0") if p]


def problems_for(path: Path, allow_large: bool = False) -> list[str]:
    found = []
    if path.suffix.lower() in BLOCKED_SUFFIXES:
        found.append(f"blocked file type {path.suffix}")
    if path.name.lower() in BLOCKED_NAMES:
        found.append("name of a Switch executable module")
    if not path.is_file():
        return found
    size = path.stat().st_size
    if size > MAX_BYTES and allow_large:
        if GENERATED_MARKER.encode() in path.read_bytes():
            found.append("generated from game code")
        return found
    if size > MAX_BYTES:
        found.append(f"{size // 1024} KiB, over the {MAX_BYTES // 1024} KiB limit")
        return found
    data = path.read_bytes()
    if GENERATED_MARKER.encode() in data:
        found.append("generated from game code")
    if KEY_LINE.search(data.decode("utf-8", errors="ignore")):
        found.append("contains a key in prod.keys format")
    return found


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--staged", action="store_true", help="check staged files only")
    args = parser.parse_args()

    failed = False
    for path in list_files(args.staged):
        for problem in problems_for(path):
            print(f"{path}: {problem}")
            failed = True
    if failed:
        print("\nGame content, keys and generated game code never go into the repository.")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

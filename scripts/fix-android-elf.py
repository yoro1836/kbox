#!/usr/bin/env python3
"""Make PT_NOTE segments acceptable to Android's dynamic linker."""

from __future__ import annotations

import os
import struct
import sys
import tempfile

PT_NOTE = 4
ELF_MAGIC = b"\x7fELF"


def fix_elf(path: str) -> bool:
    with open(path, "rb") as stream:
        data = bytearray(stream.read())

    if len(data) < 64 or data[:4] != ELF_MAGIC or data[4] != 2 or data[5] != 1:
        raise ValueError(f"{path}: expected a little-endian ELF64 file")

    phoff = struct.unpack_from("<Q", data, 32)[0]
    phentsize = struct.unpack_from("<H", data, 54)[0]
    phnum = struct.unpack_from("<H", data, 56)[0]
    if phentsize < 56 or phoff + phentsize * phnum > len(data):
        raise ValueError(f"{path}: invalid program header table")

    changed = False
    for index in range(phnum):
        offset = phoff + index * phentsize
        p_type = struct.unpack_from("<I", data, offset)[0]
        if p_type != PT_NOTE:
            continue
        p_filesz, p_memsz = struct.unpack_from("<QQ", data, offset + 32)
        if p_memsz != p_filesz:
            struct.pack_into("<Q", data, offset + 40, p_filesz)
            changed = True

    if not changed:
        return False

    mode = os.stat(path).st_mode
    directory = os.path.dirname(path) or "."
    fd, temporary = tempfile.mkstemp(prefix=".android-elf-", dir=directory)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(data)
        os.chmod(temporary, mode)
        os.replace(temporary, path)
    except Exception:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass
        raise
    return True


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} ELF", file=sys.stderr)
        return 2
    try:
        changed = fix_elf(sys.argv[1])
    except (OSError, ValueError) as error:
        print(f"android ELF fixup: {error}", file=sys.stderr)
        return 1
    print(f"android ELF fixup: {'updated' if changed else 'already valid'} {sys.argv[1]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

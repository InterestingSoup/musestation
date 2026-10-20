#!/usr/bin/env python3
"""Create separate private relay keys without printing or replacing their values."""
import argparse
import os
from pathlib import Path
import re
import secrets
import stat


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    directory = parser.parse_args().directory.absolute()
    directory.mkdir(mode=0o700, parents=True, exist_ok=True)
    for parent in (directory, *directory.parents):
        if parent.is_symlink():
            raise SystemExit("Secret directory must not traverse symlinks")
    info = directory.stat()
    if not stat.S_ISDIR(info.st_mode) or info.st_uid != os.getuid() or stat.S_IMODE(info.st_mode) != 0o700:
        raise SystemExit("Secret directory must be owned by the runtime user with mode 0700")
    values = []
    for name, pattern in (("provider-token", rb"[A-Za-z0-9_-]{32,256}"), ("console-token", rb"[0-9a-f]{64}")):
        path = directory / name
        try:
            fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
        except FileExistsError:
            pass
        else:
            with os.fdopen(fd, "wb") as stream:
                stream.write(secrets.token_hex(32).encode() + b"\n")
                stream.flush()
                os.fsync(stream.fileno())
        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
        with os.fdopen(fd, "rb") as stream:
            info = os.fstat(stream.fileno())
            if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1 or info.st_uid != os.getuid() or stat.S_IMODE(info.st_mode) not in (0o400, 0o600):
                raise SystemExit("Existing key must be a private regular file owned by the runtime user")
            value = stream.read(258).strip()
            if not re.fullmatch(pattern, value):
                raise SystemExit("Existing key format is invalid; no key was replaced")
            values.append(value)
    if values[0] == values[1]:
        raise SystemExit("Console and provider keys must be distinct")
    fd = os.open(directory, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)
    print("Separate private relay keys are ready. Existing keys were retained; values are not printed.")


if __name__ == "__main__":
    main()

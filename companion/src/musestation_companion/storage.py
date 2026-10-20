"""Require private owned state and single-writer account operations."""
import contextlib
import fcntl
import os
from pathlib import Path
import stat

STATE_FILES = ("identity.json", "pairing.json", "sdk_token",
               "identity.json.tmp", "pairing.json.tmp")


def private_state(directory: Path, *, create=True):
    directory = directory.absolute()
    if not directory.exists():
        if not create:
            return None
        directory.mkdir(mode=0o700, parents=True)
    info = directory.lstat()
    if not stat.S_ISDIR(info.st_mode) or info.st_uid != os.geteuid() or info.st_mode & 0o077:
        raise ValueError("state directory must be owned by this user with mode 0700")
    for name in STATE_FILES:
        path = directory / name
        if not path.exists() and not path.is_symlink():
            continue
        info = path.lstat()
        if (not stat.S_ISREG(info.st_mode) or info.st_uid != os.geteuid()
                or info.st_mode & 0o077 or info.st_nlink != 1 or info.st_size > 16384):
            raise ValueError("state files must be private owned regular files, at most 16KiB")
    return directory


@contextlib.contextmanager
def state_lock(directory):
    fd = os.open(directory / ".companion.lock", os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
    try:
        info = os.fstat(fd)
        if (not stat.S_ISREG(info.st_mode) or info.st_uid != os.geteuid()
                or info.st_mode & 0o077 or info.st_nlink != 1):
            raise ValueError("state lock must be a private owned regular file")
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise ValueError("another companion pairing or session is using this state") from None
        yield
    finally:
        os.close(fd)

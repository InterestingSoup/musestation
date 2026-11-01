#!/usr/bin/env python3
"""Archive the staged MuseStation title as a ProsperoStore/ShadowMountPlus folder ZIP.

ProsperoStore installs ZIPs of a <TITLEID>/ folder (eboot.bin beside sce_sys/) into a
folder ShadowMountPlus scans; it does not install .pkg files. The archive is checked
against the rules of ProsperoStore's src/install/archive.cpp and catalog.hpp after it
is written, and every unpacked file is compared with the staged source bytes.
"""
import argparse
import hashlib
import json
import os
import re
import stat
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TITLE = "PPSA99576"
CONTENT_ID = "IV0000-PPSA99576_00-MUSEGADGET000001"
ARTIFACT_LIMIT = 2 << 30  # ProsperoStore kArtifactLimit and GitHub's asset limit
ENTRY_LIMIT = 100000      # ProsperoStore kArchiveEntries
PATH_LIMIT = 512          # ProsperoStore kArchivePath
STORED = (".gguf", ".bin", ".png", ".jpg", ".dds", ".at9", ".gz", ".zip")
STAMP = (2026, 1, 1, 0, 0, 0)  # Fixed so identical input gives an identical archive


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def clean(relative):
    if not relative or len(relative) > PATH_LIMIT or "\\" in relative:
        return False
    if any(ord(c) < 32 or ord(c) == 127 for c in relative):
        return False
    return all(part not in ("", ".", "..") for part in relative.split("/"))


def collect(source):
    param = json.loads((source / "sce_sys/param.json").read_text())
    if param.get("titleId") != TITLE or param.get("contentId") != CONTENT_ID:
        raise SystemExit("param.json identity is not MuseStation")
    if param.get("conceptId") != TITLE[4:]:
        raise SystemExit("param.json conceptId does not match the title")
    if not re.fullmatch(r"[0-9]{2}\.[0-9]{3}\.[0-9]{3}", param.get("contentVersion", "")):
        raise SystemExit("param.json contentVersion is not NN.NNN.NNN")
    if not int(param.get("downloadDataSize", 0)) > 0:
        raise SystemExit("downloadDataSize must be positive so /download0 exists")
    if not (source / "eboot.bin").is_file():
        raise SystemExit("eboot.bin is missing beside sce_sys")
    directories, files = [], []
    for dirpath, dirnames, filenames in os.walk(source, followlinks=False):
        dirnames.sort()
        base = Path(dirpath)
        for name in dirnames + sorted(filenames):
            path = base / name
            relative = path.relative_to(source).as_posix()
            mode = path.lstat().st_mode
            if not clean(relative):
                raise SystemExit("Unsafe path: " + relative)
            if stat.S_ISDIR(mode):
                directories.append(relative)
            elif stat.S_ISREG(mode):
                files.append(relative)
            else:
                raise SystemExit("Links and special files cannot be archived: " + relative)
    return param, directories, files


def write(source, output, directories, files):
    partial = output.with_name(output.name + ".partial")
    with zipfile.ZipFile(partial, "w", allowZip64=True) as archive:
        root = zipfile.ZipInfo(TITLE + "/", STAMP)
        root.create_system = 3
        root.external_attr = (0o40777 << 16) | 0x10
        archive.writestr(root, b"")
        for relative in directories:
            info = zipfile.ZipInfo(f"{TITLE}/{relative}/", STAMP)
            info.create_system = 3
            info.external_attr = (0o40777 << 16) | 0x10
            archive.writestr(info, b"")
        for relative in files:
            info = zipfile.ZipInfo(f"{TITLE}/{relative}", STAMP)
            info.create_system = 3
            # The console starts an app only if its files are open to everyone.
            info.external_attr = 0o100777 << 16
            stored = relative.lower().endswith(STORED)
            info.compress_type = zipfile.ZIP_STORED if stored else zipfile.ZIP_DEFLATED
            with (source / relative).open("rb") as src, archive.open(info, "w", force_zip64=True) as dst:
                while chunk := src.read(1 << 20):
                    dst.write(chunk)
    partial.replace(output)


def verify(source, output, files):
    """Repeat ProsperoStore's archive acceptance rules, then compare every file."""
    if output.stat().st_size > ARTIFACT_LIMIT:
        raise SystemExit(f"Archive is {output.stat().st_size} bytes; ProsperoStore accepts at most {ARTIFACT_LIMIT}")
    with zipfile.ZipFile(output) as archive:
        entries = archive.infolist()
        if not entries or len(entries) > ENTRY_LIMIT:
            raise SystemExit("Archive is empty or has too many entries")
        roots = [e.filename[: -len("sce_sys/param.json")] for e in entries
                 if e.filename == "sce_sys/param.json" or e.filename.endswith("/sce_sys/param.json")]
        if roots != [TITLE + "/"]:
            raise SystemExit("Archive must hold exactly one app folder named " + TITLE)
        seen, unpacked, program = set(), {}, False
        for entry in entries:
            name = entry.filename
            if not name.startswith(TITLE + "/") or entry.flag_bits & 0x1:
                raise SystemExit("Unexpected or encrypted entry: " + name)
            relative = name[len(TITLE) + 1:].rstrip("/")
            if not relative:
                continue
            if not clean(relative) or relative in seen:
                raise SystemExit("Unsafe or duplicate entry: " + name)
            seen.add(relative)
            kind = (entry.external_attr >> 16) & 0o170000
            if entry.is_dir():
                if kind != 0o040000 or entry.file_size:
                    raise SystemExit("Bad directory entry: " + name)
                continue
            if kind != 0o100000 or (entry.external_attr >> 16) & 0o777 != 0o777:
                raise SystemExit("File entry is not a 0777 regular file: " + name)
            program |= relative == "eboot.bin"
            sha = hashlib.sha256()
            with archive.open(entry) as stream:
                while chunk := stream.read(1 << 20):
                    sha.update(chunk)
            unpacked[relative] = sha.hexdigest()
        if not program:
            raise SystemExit("Archive has no eboot.bin beside sce_sys")
    if set(unpacked) != set(files):
        raise SystemExit("Archive file list differs from the staged title")
    expected = {relative: digest(source / relative) for relative in files}
    for relative, value in unpacked.items():
        if value != expected[relative]:
            raise SystemExit("Unpacked bytes differ: " + relative)
    return expected


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("source", nargs="?", type=Path, default=ROOT / "build/native/title" / TITLE)
    parser.add_argument("output", nargs="?", type=Path, default=ROOT / "dist/zip")
    args = parser.parse_args()
    source = args.source.resolve()
    if source.name != TITLE:
        raise SystemExit("Source folder must be named " + TITLE)
    args.output.mkdir(parents=True, exist_ok=True)
    output = args.output.resolve() / (TITLE + ".zip")
    param, directories, files = collect(source)
    write(source, output, directories, files)
    hashes = verify(source, output, files)
    sha = digest(output)
    (output.parent / (output.name + ".sha256")).write_text(f"{sha}  {output.name}\n")
    report = {
        "title": param["localizedParameters"]["en-US"]["titleName"],
        "titleId": TITLE,
        "contentId": CONTENT_ID,
        "version": param["contentVersion"],
        "format": "zip",
        "archive": output.name,
        "bytes": output.stat().st_size,
        "sha256": sha,
        "unpackedBytes": sum((source / f).stat().st_size for f in files),
        "fileCount": len(files),
        "install": f"Copy {TITLE}/ into a ShadowMountPlus folder such as /data/homebrew, or install through a ProsperoStore catalog",
        "checks": ["ProsperoStore archive layout", "0777 regular files and folders only",
                   "2 GiB artifact limit", "unpacked SHA-256 equals staged source"],
        "hardwareVerified": False,
        "files": [{"path": f, "bytes": (source / f).stat().st_size, "sha256": hashes[f]} for f in files],
    }
    (output.parent / (output.name + ".report.json")).write_text(json.dumps(report, indent=2) + "\n")
    print(f"{output}: {report['fileCount']} files, {report['bytes']} bytes, sha256 {sha}")


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Snapshot trusted, already fetched native dependencies. Never processes Muse source."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import struct
import zlib


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for part in iter(lambda: f.read(1024 * 1024), b''):
            h.update(part)
    return h.hexdigest()


def copy(source, target):
    if source.is_dir():
        # Materialize dependency symlinks; the final snapshot contains regular files only.
        shutil.copytree(source, target, symlinks=False,
                        ignore=shutil.ignore_patterns('.git', '__pycache__', '*.pyc'))
    else:
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)


def png(path, width, height):
    """Original geometric app icon; no title-specific or third-party artwork."""
    def chunk(kind, payload):
        return struct.pack('>I', len(payload)) + kind + payload + struct.pack('>I', zlib.crc32(kind + payload))
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        for x in range(width):
            inside = width * .32 < x < width * .68 and height * .30 < y < height * .70
            rows.extend((85, 222, 192) if inside else (12, 24, 38))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(rows, 6)) + chunk(b'IEND', b''))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--project', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    project = args.project.resolve(strict=True)
    output = args.output.absolute()
    if output.exists():
        raise SystemExit('refusing to overwrite an existing kit; choose a new version directory')
    output.mkdir(parents=True, mode=0o700)
    source = project / 'vendor/native-ui'
    cache = project / 'build/native/muse'
    template = output / 'template'
    for name in ['Makefile', 'LICENSE', 'THIRD_PARTY_NOTICES.md', 'tools', 'tooling', 'runtime',
                 'assets/fonts', 'src/gfx', 'src/core', 'src/ui', 'src/audio',
                 'src/platform/ps5', 'src/runtime', 'src/third_party', 'third_party/fonts']:
        if (source / name).exists():
            copy(source / name, template / name)
    (template / 'sce_sys').mkdir()
    copy(source / 'sce_sys/param.json', template / 'sce_sys/param.json')
    for name, w, h in [('icon0.png', 512, 512), ('pic1.png', 1920, 1080)]:
        png(template / 'sce_sys' / name, w, h)
    copy(cache / 'runtime/libc.prx', template / 'runtime/libc.prx')
    expected = (template / 'runtime/libc.prx.sha256').read_text().split()[0]
    if digest(template / 'runtime/libc.prx') != expected:
        raise SystemExit('cached clean-room native runtime checksum mismatch')
    copy(cache / '.deps/native/ps5-payload-sdk', output / 'deps/native/ps5-payload-sdk')
    copy(cache / '.deps/native/zlib/root', output / 'deps/native/zlib/root')
    ogl = cache / '.deps/ps5-opengl/ps5-opengl-sdk-1.0.2'
    copy(ogl / 'sdk', output / 'deps/opengl/sdk')
    for name in ['LICENSE', 'LICENSES', 'THIRD_PARTY_NOTICES.md']:
        if (ogl / name).exists():
            copy(ogl / name, template / 'assets/licenses/opengl' / name)
    # Runtime/source collector already records exact SDK/LLVM/zlib/Mesa provenance.
    notices = project / 'assets/inference/licenses'
    if notices.is_dir():
        copy(notices, template / 'assets/licenses/native-stack')
    copy(source / 'LICENSE', template / 'assets/licenses/homebrew-ui-LICENSE')
    if (source / 'third_party/fonts').exists():
        copy(source / 'third_party/fonts', template / 'assets/licenses/fonts')
    for name in ['run-native.sh', 'offline-dependencies.sh']:
        copy(project / 'relay/build' / name, output / 'recipe' / name)
    copy(output / 'recipe/offline-dependencies.sh', template / 'tools/setup-native-dependencies.sh')
    records = []
    for path in sorted(output.rglob('*')):
        if path.is_symlink() or not (path.is_dir() or path.is_file()):
            raise SystemExit('kit must contain only regular files/directories')
        if path.is_file():
            executable = bool(path.stat().st_mode & stat.S_IXUSR)
            path.chmod(0o555 if executable else 0o444)
            records.append({'path': path.relative_to(output).as_posix(), 'bytes': path.stat().st_size,
                            'sha256': digest(path), 'executable': executable})
    manifest = {'schema': 1, 'recipe': 'muse-native-cpp-v1', 'files': records}
    manifest_path = output / 'kit.json'
    manifest_path.write_text(json.dumps(manifest, sort_keys=True, indent=2) + '\n')
    manifest_path.chmod(0o444)
    for path in sorted(output.rglob('*'), reverse=True):
        if path.is_dir():
            path.chmod(0o555)
    output.chmod(0o555)
    print(json.dumps({'kit': str(output), 'files': len(records), 'manifest_sha256': digest(manifest_path)}))


if __name__ == '__main__':
    main()

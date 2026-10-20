"""Fixed-recipe native PS5 builder; this module is used only by the socket-owning worker."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import selectors
import shutil
import stat
import subprocess
import time

MAX_FILES = 32
MAX_FILE_BYTES = 256 * 1024
MAX_SOURCE_BYTES = 1024 * 1024
MAX_ARTIFACT_BYTES = 128 * 1024 * 1024
MAX_OUTPUT_BYTES = 384 * 1024 * 1024
MAX_LOG_BYTES = 2 * 1024 * 1024
OWNER_LABEL = 'org.musestation.relay.builder'
NAMESPACE_LABEL = 'org.musestation.relay.namespace'


class BuildError(RuntimeError):
    """Bounded diagnostic suitable for durable job failure reporting."""
    def __init__(self, message, log_paths=None):
        super().__init__(message)
        self.message = str(message)
        self.log_paths = list(log_paths or [])


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def validate_source(manifest: dict) -> tuple[dict[str, str], str]:
    if not isinstance(manifest, dict) or manifest.get('language') != 'cpp':
        raise BuildError('source language must be cpp')
    files = manifest.get('files')
    if not isinstance(files, dict) or not 1 <= len(files) <= MAX_FILES or 'main.cpp' not in files:
        raise BuildError('source requires main.cpp and at most 32 files')
    total = 0
    for name, text in files.items():
        if not isinstance(name, str) or not isinstance(text, str):
            raise BuildError('source files must map path strings to UTF-8 strings')
        parts = PurePosixPath(name).parts
        if (name != 'main.cpp' and not re.fullmatch(r'include/[A-Za-z0-9_/-]+\.(?:h|hpp)', name)) or any(
                part in ('', '.', '..') for part in name.split('/')) or len(name) > 160:
            raise BuildError('only main.cpp and include/<name>.h or .hpp are accepted')
        if not parts or name != PurePosixPath(name).as_posix() or '\x00' in text:
            raise BuildError('source contains an invalid path or NUL')
        try:
            size = len(text.encode('utf-8'))
        except UnicodeError as exc:
            raise BuildError('source contains invalid UTF-8') from exc
        if not 1 <= size <= MAX_FILE_BYTES:
            raise BuildError('each source file must contain 1..262144 UTF-8 bytes')
        total += size
    if total > MAX_SOURCE_BYTES:
        raise BuildError('source exceeds 1 MiB total')
    encoded = json.dumps(files, ensure_ascii=False, sort_keys=True, separators=(',', ':')).encode('utf-8')
    return dict(files), hashlib.sha256(encoded).hexdigest()


def validate_identity(identity: dict) -> dict:
    if not isinstance(identity, dict) or set(identity) != {'title_id', 'content_id', 'name', 'version'}:
        raise BuildError('assigned identity has unexpected fields')
    title, content, name, version = (identity[k] for k in ('title_id', 'content_id', 'name', 'version'))
    if not all(isinstance(x, str) for x in (title, content, name, version)):
        raise BuildError('assigned identity values must be strings')
    if not re.fullmatch(r'PPSA99[0-9]{3}', title) or title in ('PPSA99000', 'PPSA99576'):
        raise BuildError('assigned title must be in the generated-app range and cannot replace ProsperoStore or MuseStation')
    if not re.fullmatch(r'IV0000-' + re.escape(title) + r'_00-[A-Z0-9]{16}', content):
        raise BuildError('assigned content identity does not match title')
    if version != '01.000.000' or not 1 <= len(name.encode('utf-8')) <= 80 or any(ord(c) < 32 for c in name):
        raise BuildError('invalid display name or initial application version')
    return dict(identity)


def checked_tree(root: Path, limit: int, max_files: int = 4096) -> dict[str, Path]:
    result = {}
    total = 0
    for directory, dirs, names in os.walk(root, followlinks=False):
        for name in dirs + names:
            path = Path(directory) / name
            info = path.lstat()
            if stat.S_ISDIR(info.st_mode):
                continue
            if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1:
                raise BuildError('build output contains a symlink, special file or hard link')
            total += info.st_size
            result[path.relative_to(root).as_posix()] = path
            if total > limit or len(result) > max_files:
                raise BuildError('build output exceeded its file/byte quota')
    return result


class Builder:
    def __init__(self, project_root, kit_dir, compiler_image, package_image, *, namespace, docker='docker',
                 compile_timeout=900, package_timeout=300):
        # project_root is retained for the worker interface; it is NEVER mounted.
        self.project_root = Path(project_root)
        self.kit_dir = Path(kit_dir).absolute()
        self.compiler_image = compiler_image
        self.package_image = package_image
        self.docker = docker
        self.compile_timeout = compile_timeout
        self.package_timeout = package_timeout
        # The private Store root, not a submitted field or the shared kit, owns
        # recovery. Independent fixture/production workers must never stop each other.
        owner = Path(namespace)
        if not owner.is_absolute() or not owner.is_dir():
            raise BuildError('builder namespace must be an existing absolute private data directory')
        self.namespace_root = owner.resolve(strict=True)
        self.namespace_hash = hashlib.sha256(str(self.namespace_root).encode('utf-8')).hexdigest()
        self.container_name = 'muse-relay-' + self.namespace_hash[:16] + '-job'

    def _image(self, image):
        if not isinstance(image, str) or not image or image.startswith('-'):
            raise BuildError('invalid trusted Docker image configuration')
        try:
            proc = subprocess.run([self.docker, 'image', 'inspect', '--format', '{{.Id}}', image],
                                  capture_output=True, timeout=20, check=True)
        except (OSError, subprocess.SubprocessError) as exc:
            raise BuildError('configured build image is unavailable locally; worker never pulls images') from exc
        result = proc.stdout.decode('ascii', errors='replace').strip()
        if not re.fullmatch(r'sha256:[0-9a-f]{64}', result):
            raise BuildError('Docker returned an invalid immutable image ID')
        return result

    def recover_containers(self):
        """Call under the exclusive worker lock, before recovering interrupted jobs."""
        try:
            probe = subprocess.run([self.docker, 'container', 'inspect', self.container_name],
                                   capture_output=True, timeout=20)
            if probe.returncode:
                if b'No such container' in probe.stderr or b'No such object' in probe.stderr:
                    return False
                raise BuildError('cannot inspect the reserved relay compiler container')
            record = json.loads(probe.stdout)[0]
            labels = record['Config']['Labels']
            if labels.get(OWNER_LABEL) != '1' or labels.get(NAMESPACE_LABEL) != self.namespace_hash:
                raise BuildError('reserved compiler container name is owned by another service; refusing to stop it')
            cid = record['Id']
            if not isinstance(cid, str) or not re.fullmatch(r'[0-9a-f]{64}', cid):
                raise BuildError('reserved compiler container has invalid identity')
            subprocess.run([self.docker, 'rm', '-f', cid], check=True,
                           capture_output=True, timeout=20)
            return True
        except (OSError, subprocess.SubprocessError, KeyError, ValueError, TypeError) as exc:
            raise BuildError('failed to stop the owned interrupted compiler container') from exc

    def _kit(self):
        if self.kit_dir.is_symlink() or not self.kit_dir.is_dir():
            raise BuildError('native kit must be a real directory')
        files = checked_tree(self.kit_dir, 768 * 1024 * 1024, 20000)
        try:
            manifest = json.loads((self.kit_dir / 'kit.json').read_text())
            expected = {entry['path']: entry for entry in manifest['files']}
            if manifest['schema'] != 1 or manifest['recipe'] != 'muse-native-cpp-v1' or len(expected) != len(manifest['files']):
                raise ValueError()
            if set(expected) | {'kit.json'} != set(files):
                raise ValueError()
            for name, entry in expected.items():
                path = files[name]
                if path.stat().st_mode & 0o222 or path.stat().st_size != entry['bytes'] or sha256(path) != entry['sha256']:
                    raise ValueError()
            if (self.kit_dir / 'kit.json').stat().st_mode & 0o222:
                raise ValueError()
        except (OSError, ValueError, TypeError, KeyError) as exc:
            raise BuildError('native kit integrity verification failed') from exc
        return sha256(self.kit_dir / 'kit.json')

    @staticmethod
    def _mount(path: Path, destination: str, readonly: bool):
        # Docker --mount is comma-separated; reject metacharacters in trusted paths too.
        source = str(path)
        if ',' in source or '\n' in source or not path.is_absolute():
            raise BuildError('build paths must be absolute and cannot contain comma/newline')
        return ['--mount', f'type=bind,src={source},dst={destination}' + (',readonly' if readonly else '')]

    def _command(self, name, image, mounts, *, package=False):
        command = [self.docker, 'run', '--rm', '--pull=never', '--name', name, '--network', 'none',
                   '--label', OWNER_LABEL + '=1',
                   '--label', NAMESPACE_LABEL + '=' + self.namespace_hash,
                   '--log-driver', 'none',
                   '--read-only', '--cap-drop', 'ALL', '--security-opt', 'no-new-privileges',
                   '--pids-limit', '128', '--memory', '3g', '--memory-swap', '3g', '--cpus', '1',
                   '--ulimit', 'nofile=1024:1024', '--ulimit', 'fsize=134217728:134217728',
                   '--user', f'{os.getuid()}:{os.getgid()}', '--workdir', '/work',
                   '--tmpfs', '/tmp:rw,noexec,nosuid,size=128m,mode=1777',
                   '--tmpfs', '/work:rw,exec,nosuid,size=1g,mode=1777']
        for path, destination, readonly in mounts:
            command += self._mount(path, destination, readonly)
        if package:
            command += ['--env', 'DOTNET_GCHeapHardLimitPercent=5A', '--env', 'DOTNET_PROCESSOR_COUNT=1',
                        '--entrypoint', '/usr/bin/timeout', image, '--signal=TERM', '--kill-after=10s',
                        str(self.package_timeout) + 's', 'dotnet', '/app/Muse.Package.dll',
                        'build-generated', '/input', '/output']
        else:
            command += ['--entrypoint', '/usr/bin/timeout', image, '--signal=TERM', '--kill-after=10s',
                        str(self.compile_timeout) + 's', '/bin/bash', '/kit/recipe/run-native.sh']
        return command

    def _run(self, command, name, logfile: Path, output: Path, timeout):
        started = time.monotonic()
        output_count = 0
        last_quota_check = 0.0
        failure = None
        process = None
        cidfile = logfile.with_suffix('.cid')
        command = list(command)
        if command[:2] == [self.docker, 'run']:
            if cidfile.exists() or cidfile.is_symlink():
                raise BuildError('refusing to reuse container ownership record')
            command[2:2] = ['--cidfile', str(cidfile)]
        selector = selectors.DefaultSelector()
        try:
            with logfile.open('xb') as log:
                process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                           stdin=subprocess.DEVNULL, start_new_session=True)
                selector.register(process.stdout, selectors.EVENT_READ)
                while selector.get_map():
                    now = time.monotonic()
                    if now - started > timeout:
                        raise BuildError('container exceeded its wall-clock limit')
                    if now - last_quota_check >= 0.5:
                        checked_tree(output, MAX_OUTPUT_BYTES)
                        last_quota_check = now
                    for key, _ in selector.select(timeout=0.25):
                        data = os.read(key.fileobj.fileno(), 16384)
                        if not data:
                            selector.unregister(key.fileobj)
                            break
                        output_count += len(data)
                        if output_count > MAX_LOG_BYTES:
                            raise BuildError('compiler diagnostics exceeded the 2 MiB log limit')
                        log.write(data)
                        log.flush()
                returncode = process.wait(timeout=5)
                log.flush()
                os.fsync(log.fileno())
                if returncode:
                    # The container has only public kit/source inputs, so compiler errors
                    # are useful Muse diagnostics. Keep them bounded and strip control bytes.
                    with logfile.open('rb') as previous:
                        previous.seek(max(0, logfile.stat().st_size - 640))
                        tail = previous.read().decode('utf-8', errors='replace')
                    tail = ''.join(c if c in '\n\t' or ord(c) >= 32 else '?' for c in tail)
                    raise BuildError(f'container exited with status {returncode}; {logfile.name}: {tail}')
                checked_tree(output, MAX_OUTPUT_BYTES)
        except (OSError, subprocess.SubprocessError, BuildError) as exc:
            failure = exc
        finally:
            selector.close()
            if failure is not None:
                try:
                    # A name collision may belong to another process. Remove only
                    # the container this invocation actually created, never by name.
                    cid = cidfile.read_text().strip()
                    if re.fullmatch(r'[0-9a-f]{64}', cid):
                        subprocess.run([self.docker, 'rm', '-f', cid], capture_output=True, timeout=15)
                except (OSError, subprocess.SubprocessError):
                    pass
                if process is not None and process.poll() is None:
                    process.kill()
                    process.wait(timeout=5)
            if process is not None and process.stdout is not None:
                process.stdout.close()
        if failure:
            raise BuildError(f'{failure}; log={logfile.name}', [str(logfile)]) from failure
        return round(time.monotonic() - started, 3)

    def _verify(self, title: Path, output: Path, identity: dict):
        package = output / (identity['content_id'] + '.pkg')
        report_path = output / (package.name + '.report.json')
        checked_tree(output, MAX_OUTPUT_BYTES)
        if not package.is_file() or not 0 < package.stat().st_size <= MAX_ARTIFACT_BYTES:
            raise BuildError('package is missing or exceeds the 128 MiB v1 limit')
        try:
            if report_path.stat().st_size > MAX_LOG_BYTES:
                raise ValueError('oversized verification report')
            report = json.loads(report_path.read_text())
            ptitle = report['Title']
            if (report['SchemaVersion'] != 1 or report['GeneratedApplication'] is not True or
                report['HardwareVerified'] is not False or not report['StructuralValidation']['Accepted'] or
                not report['ModuleReadiness']['IsLaunchReady'] or not report['NapsIndexValidation']['SerializedRoundTrip'] or
                ptitle['TitleId'] != identity['title_id'] or ptitle['ContentId'] != identity['content_id'] or
                ptitle['Name'] != identity['name'] or ptitle['Version'] != identity['version']):
                raise ValueError('package identity or readiness mismatch')
            actual = checked_tree(title, MAX_ARTIFACT_BYTES)
            entries = {entry['path']: entry for entry in report['Files']}
            if len(entries) != len(report['Files']) or set(entries) != set(actual) or report['RoundTripFileCount'] != len(actual):
                raise ValueError('full package readback did not cover every source file')
            for path, source in actual.items():
                entry = entries[path]
                if entry['sourceSha256'] != sha256(source):
                    raise ValueError('package source hash mismatch')
                # Only the public packager's documented metadata normalization is accepted.
                if entry['transformation'] != 'none' and path != 'sce_sys/param.json':
                    raise ValueError('unexpected package transformation')
            digest = sha256(package)
            if report['Bytes'] != package.stat().st_size or report['Sha256'].lower() != digest:
                raise ValueError('final package hash/length mismatch')
        except (OSError, ValueError, TypeError, KeyError) as exc:
            raise BuildError(f'package readback report rejected: {exc}') from exc
        return {'artifact_path': str(package), 'bytes': package.stat().st_size, 'sha256': digest,
                'title_id': identity['title_id'], 'content_id': identity['content_id'], 'version': identity['version'],
                'verification_path': str(report_path), 'hardware_verified': False}

    def build(self, job_dir, source_manifest, identity, output_dir):
        files, source_digest = validate_source(source_manifest)
        identity = validate_identity(identity)
        job = Path(job_dir).absolute()
        output = Path(output_dir).absolute()
        if job.is_symlink() or output.is_symlink() or output.parent != job or output.name != 'out':
            raise BuildError('builder output must be the private job/out directory')
        job.mkdir(mode=0o700, parents=True, exist_ok=True)
        if job.resolve() != job:
            raise BuildError('job path may not traverse symlinks')
        if output.exists() and any(output.iterdir()):
            raise BuildError('refusing to overwrite an existing build output')
        output.mkdir(mode=0o700, exist_ok=True)
        build = job / 'native-build'
        try:
            build.mkdir(mode=0o700)
        except FileExistsError as exc:
            raise BuildError('build already attempted; explicit new job required') from exc
        kit_digest = self._kit()
        compiler_id, package_id = self._image(self.compiler_image), self._image(self.package_image)
        inputs, native = build / 'input', build / 'native'
        inputs.mkdir(mode=0o700)
        native.mkdir(mode=0o700)
        for name, text in files.items():
            path = inputs / 'source' / name
            path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
            path.write_text(text, encoding='utf-8')
            path.chmod(0o400)
        (inputs / 'identity.json').write_text(json.dumps(identity, sort_keys=True) + '\n')
        (inputs / 'identity.json').chmod(0o400)
        compile_log, package_log = job / 'compiler.log', job / 'package.log'
        name = self.container_name
        try:
            compile_seconds = self._run(self._command(name, compiler_id,
                [(self.kit_dir, '/kit', True), (inputs, '/input', True), (native, '/output', False)]),
                name, compile_log, native, self.compile_timeout)
            title = native / 'title'
            if not (title / 'eboot.bin').is_file():
                raise BuildError('compiler did not produce a native title')
            checked_tree(title, MAX_ARTIFACT_BYTES)
            name = self.container_name
            package_seconds = self._run(self._command(name, package_id,
                [(title, '/input', True), (output, '/output', False)], package=True),
                name, package_log, output, self.package_timeout)
            result = self._verify(title, output, identity)
            result.update(source_sha256=source_digest, kit_sha256=kit_digest,
                          compiler_image=compiler_id, package_image=package_id,
                          compiler_log=str(compile_log), package_log=str(package_log),
                          compile_seconds=compile_seconds, package_seconds=package_seconds)
            (job / 'build-result.json').write_text(json.dumps(result, indent=2, sort_keys=True) + '\n')
            return result
        finally:
            # No dependency copies/objects survive: those lived only in the container tmpfs.
            # Retain <=1MiB source, bounded logs, and the final package/report for triage.
            shutil.rmtree(native, ignore_errors=True)
            for path in output.glob('.muse-package-*'):
                if path.is_dir() and not path.is_symlink():
                    shutil.rmtree(path, ignore_errors=True)

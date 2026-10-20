"""Separate, serialized worker. Only this service receives the Docker socket.

Compiler/package images and native kit come from operator configuration, never
incoming requests. A process lock makes interrupted-build recovery unambiguous.
"""
from __future__ import annotations

import fcntl
import json
import os
import signal
import stat
import threading
from pathlib import Path

from .store import RelayError, SafeDir, Store


def collect_diagnostics(store, id, paths):
    job_dir = store.path / "work" / id
    files = []
    for path in list(paths)[:3]:
        path = Path(path)
        try:
            relative = path.relative_to(job_dir)
            if ".." in relative.parts or path.suffix != ".log":
                continue
            with SafeDir(path.parent, False) as parent:
                fd = parent.open(path.name)
            with os.fdopen(fd, "rb") as stream:
                size = os.fstat(stream.fileno()).st_size
                # JSON escaping may expand control bytes sixfold; cap each tail
                # at 12 KiB, <=216 KiB worst-case for three logs.
                stream.seek(max(0, size - 12 * 1024))
                tail = stream.read(12 * 1024).decode("utf-8", errors="replace")
                files.append({"name": str(relative), "original_bytes": size,
                              "tail": tail, "truncated": size > 12 * 1024})
        except (OSError, RelayError, ValueError):
            continue
    if files:
        store.save_diagnostics(id, files)


def run_one(store, builder):
    job = store.claim_job()
    if job is None:
        return False
    id = job["id"]
    job_dir = store.path / "work" / id
    output_dir = job_dir / "out"
    try:
        with SafeDir(output_dir):
            pass
        result = builder.build(job_dir, job["source_manifest"], job["identity"], output_dir)
        collect_diagnostics(store, id, [result[k] for k in ("compiler_log", "package_log") if result.get(k)])
        store.finish_job(id, result)
    except Exception as error:
        # Compile diagnostics are deliberately exported to Muse for a new request.
        # Keep them bounded and avoid an arbitrary exception traceback/body.
        detail = getattr(error, "message", str(error)) or type(error).__name__
        detail = detail.encode("utf-8", errors="replace")[:1000].decode("utf-8", errors="ignore")
        try:
            paths = getattr(error, "log_paths", [])
            if isinstance(paths, dict):
                paths = paths.values()
            collect_diagnostics(store, id, paths)
        except (RelayError, OSError):
            detail = "Could not preserve compiler diagnostics. " + detail
            detail = detail.encode("utf-8")[:1000].decode("utf-8", errors="ignore")
        store.fail_job(id, detail)
    else:
        # Cleanup cannot reverse a durably completed/verified result. The served
        # copy is already committed; retain the redundant file if unlink fails.
        try:
            with SafeDir(output_dir, False) as directory:
                os.unlink(Path(result["artifact_path"]).name, dir_fd=directory.fd)
                os.fsync(directory.fd)
        except OSError:
            print(json.dumps({"event": "private_package_cleanup_failed", "job_id": id}), flush=True)
    return True


def recover_jobs(store):
    """Called only after owned containers stop, while holding the worker lock."""
    with store.db() as db:
        rows = db.execute("SELECT id FROM jobs WHERE state='building'").fetchall()
    for row in rows:
        id = row["id"]
        with store.db() as db:
            present = store.diagnostics_reference(db, id)
        if not present:
            try:
                root = store.path / "work" / id
                collect_diagnostics(store, id, [root / "compiler.log", root / "package.log"])
            except (RelayError, OSError):
                print(json.dumps({"event": "interrupted_diagnostics_unavailable", "job_id": id}), flush=True)
    return store.recover_interrupted()


def builder_for_store(store, kit, compiler_image, package_image):
    from .builder import Builder
    return Builder(project_root=Path(kit), kit_dir=Path(kit), compiler_image=compiler_image,
                   package_image=package_image, namespace=str(store.path))


def main():
    os.umask(0o077)
    data = os.environ.get("MUSE_RELAY_DATA_DIR", "/data")
    kit = os.environ["MUSE_RELAY_NATIVE_KIT"]
    build_image = os.environ["MUSE_BUILD_IMAGE"]
    package_image = os.environ["MUSE_PACKAGE_IMAGE"]
    store = Store(data)
    lock = os.open("worker.lock", os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600, dir_fd=store.root.fd)
    info = os.fstat(lock)
    if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1:
        raise ValueError("Worker lock must be a regular private file")
    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    builder = builder_for_store(store, kit, build_image, package_image)
    # Stop only the builder's exact owned/labelled container before freeing the
    # durable single-build slot. A surviving orphan must not overlap a new build.
    builder.recover_containers()
    recovered = recover_jobs(store)
    stop = threading.Event()
    for sig in (signal.SIGTERM, signal.SIGINT):
        signal.signal(sig, lambda *_: stop.set())
    def heartbeat():
        while not stop.is_set():
            try:
                store.worker_heartbeat()
            except Exception:
                pass
            stop.wait(10)
    pulse = threading.Thread(target=heartbeat, daemon=True)
    pulse.start()
    print(json.dumps({"event": "compiler_worker_started", "interrupted_jobs_failed": recovered}), flush=True)
    try:
        while not stop.is_set():
            if not run_one(store, builder):
                stop.wait(2)
    finally:
        stop.set()
        pulse.join(timeout=5)
        store.close()
        os.close(lock)


if __name__ == "__main__":
    main()

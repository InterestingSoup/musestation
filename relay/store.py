"""Durable relay state. Incoming content is data; only the fixed Builder compiles it.

The database and private data directory must be owned by the relay/worker account.
External writers receive access to the exchange directory, never this directory.
"""
from __future__ import annotations

import contextlib
import ctypes
import errno
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import sqlite3
import stat
import struct
import time
import uuid

MAX_JSON = 2 * 1024 * 1024  # escaped source envelope; actual code <= 1 MiB
MAX_LOG = 8 * 1024 * 1024
MAX_PACKAGE = 1024 * 1024 * 1024
ID = re.compile(r"[a-z][0-9a-f]{32}\Z")
SHA = re.compile(r"[0-9a-f]{64}\Z")
RESERVED_TITLES = frozenset(("PPSA99000", "PPSA99576"))  # ProsperoStore, MuseStation
MAX_INVENTORY_TITLES = 4096


def console_inventory(value):
    """An authenticated console report, not an attestation of scan completeness.

    The client reads ShadowMountPlus immediately before approval. Its API omits
    some stale/disconnected registrations, so installation must still check the
    OS for an existing title. Never let provider content supply this report.
    """
    fields(value, ("source", "api_version", "title_ids"))
    if value["source"] != "shadowmountplus" or type(value["api_version"]) is not int or value["api_version"] != 1:
        raise RelayError(400, "Approval requires ShadowMountPlus API v1 inventory")
    titles = value["title_ids"]
    if not isinstance(titles, list) or len(titles) > MAX_INVENTORY_TITLES:
        raise RelayError(400, "Console inventory must contain at most 4096 title IDs")
    if any(not isinstance(title, str) or not re.fullmatch(r"(?:PPSA|CUSA|LAPY|FAKE)[0-9]{5}", title) for title in titles):
        raise RelayError(400, "Console inventory contains an invalid title ID")
    if len(set(titles)) != len(titles):
        raise RelayError(400, "Console inventory contains duplicate title IDs")
    return {"source": "shadowmountplus", "api_version": 1, "title_ids": sorted(titles)}


def rename_noreplace(directory_fd, temporary, name):
    """Linux atomic no-replace publish, with no intermediate hard-link alias.

    The supported relay runs in Linux Docker. Fail closed on unsupported libc or
    filesystems rather than fall back to an overwriting rename or link/unlink.
    """
    library = ctypes.CDLL(None, use_errno=True)
    try:
        rename = library.renameat2
    except AttributeError:
        raise OSError(errno.ENOTSUP, "Atomic no-replace rename requires Linux renameat2") from None
    rename.argtypes = (ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint)
    rename.restype = ctypes.c_int
    if rename(directory_fd, os.fsencode(temporary), directory_fd, os.fsencode(name), 1):
        code = ctypes.get_errno()
        raise OSError(code, os.strerror(code), name)


class RelayError(Exception):
    def __init__(self, status, message):
        super().__init__(message)
        self.status = status
        self.message = message


def canonical(value):
    return json.dumps(value, ensure_ascii=False, allow_nan=False,
                      sort_keys=True, separators=(",", ":")).encode("utf-8")


def strict_json(raw):
    if not isinstance(raw, bytes) or len(raw) > MAX_JSON:
        raise RelayError(413, "JSON envelope is too large")
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError("duplicate JSON key")
            result[key] = value
        return result
    def constant(_):
        raise ValueError("non-finite JSON number")
    try:
        value = json.loads(raw.decode("utf-8"), object_pairs_hook=pairs,
                           parse_constant=constant)
        # Reject surrogate code points before content is persisted/exported.
        canonical(value)
    except (ValueError, UnicodeError, RecursionError, TypeError):
        raise RelayError(400, "Malformed UTF-8 JSON or duplicate object key") from None
    if not isinstance(value, dict):
        raise RelayError(400, "Expected a JSON object")
    return value


def fields(value, required, optional=()):
    if not isinstance(value, dict) or not set(required) <= value.keys() or set(value) - set(required) - set(optional):
        raise RelayError(400, "Missing or unsupported object fields")


def text(value, limit, label, empty=False):
    if not isinstance(value, str) or "\0" in value:
        raise RelayError(400, f"Invalid {label}")
    try:
        length = len(value.encode("utf-8"))
    except UnicodeError:
        raise RelayError(400, f"Invalid UTF-8 {label}") from None
    if length > limit or (not empty and not value.strip()):
        raise RelayError(400, f"Empty or oversized {label}")
    return value


def identifier(value, prefix=None):
    if not isinstance(value, str) or not ID.fullmatch(value) or (prefix and not value.startswith(prefix)):
        raise RelayError(400, "Invalid relay identifier")
    return value


def source_manifest(value):
    fields(value, ("schema", "type", "job_id", "approved_revision", "language", "files"))
    if type(value["schema"]) is not int or value["schema"] != 1 or value["type"] != "source" or value["language"] != "cpp":
        raise RelayError(400, "Expected schema-1 C++ source submission")
    identifier(value["job_id"], "j")
    if not isinstance(value["approved_revision"], str) or not SHA.fullmatch(value["approved_revision"]):
        raise RelayError(400, "Invalid approved revision")
    files = value["files"]
    if not isinstance(files, dict) or not 1 <= len(files) <= 32 or "main.cpp" not in files:
        raise RelayError(400, "Source requires main.cpp and at most 32 files")
    total = 0
    for name, body in files.items():
        if not isinstance(name, str) or not re.fullmatch(r"(?:main\.cpp|include/[A-Za-z0-9_/-]+\.(?:h|hpp))", name):
            raise RelayError(400, "Only main.cpp and include headers are allowed")
        if len(name) > 160 or any(part in ("", ".", "..") for part in name.split("/")):
            raise RelayError(400, "Invalid source path")
        total += len(text(body, 256 * 1024, "source file").encode("utf-8"))
    if total > 1024 * 1024:
        raise RelayError(413, "Source exceeds 1 MiB")
    return hashlib.sha256(canonical(files)).hexdigest()


class SafeDir:
    """Directory-fd operations refuse symlink components and final symlink files."""
    def __init__(self, path, create=True):
        self.path = Path(path)
        if not self.path.is_absolute() or ".." in self.path.parts:
            raise ValueError("Storage path must be absolute without parent traversal")
        fd = os.open("/", os.O_RDONLY | os.O_DIRECTORY)
        try:
            for component in self.path.parts[1:]:
                if create:
                    try:
                        os.mkdir(component, 0o700, dir_fd=fd)
                    except FileExistsError:
                        pass
                new = os.open(component, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=fd)
                os.close(fd)
                fd = new
            self.fd = fd
        except BaseException:
            os.close(fd)
            raise

    def close(self):
        if self.fd is not None:
            os.close(self.fd)
            self.fd = None

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    @staticmethod
    def name(name):
        if not isinstance(name, str) or not re.fullmatch(r"[A-Za-z0-9_.-]{1,180}", name) or name in (".", ".."):
            raise RelayError(400, "Invalid storage filename")
        return name

    def open(self, name):
        fd = os.open(self.name(name), os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=self.fd)
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1:
            os.close(fd)
            raise RelayError(400, "Storage entry must be a regular unlinked-alias-free file")
        return fd

    def read(self, name, limit):
        fd = self.open(name)
        with os.fdopen(fd, "rb") as stream:
            before = os.fstat(stream.fileno())
            if before.st_size > limit:
                raise RelayError(413, "Storage entry is too large")
            raw = stream.read(limit + 1)
            if len(raw) > limit:
                raise RelayError(413, "Storage entry is too large")
            after = os.fstat(stream.fileno())
            if before.st_size != after.st_size or before.st_mtime_ns != after.st_mtime_ns or before.st_ctime_ns != after.st_ctime_ns:
                raise RelayError(409, "Storage input changed during read; publish files atomically")
            return raw

    def write(self, name, raw):
        """Atomic immutable publish; identical replay is accepted, mutation rejected."""
        self.name(name)
        temporary = ".tmp-" + uuid.uuid4().hex
        fd = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW,
                     0o600, dir_fd=self.fd)
        try:
            with os.fdopen(fd, "wb") as stream:
                stream.write(raw)
                stream.flush()
                os.fsync(stream.fileno())
            try:
                rename_noreplace(self.fd, temporary, name)
            except FileExistsError:
                try:
                    existing = self.read(name, len(raw))
                except RelayError as error:
                    if error.status == 413:
                        raise RelayError(409, "Immutable storage entry already differs") from None
                    raise
                if existing != raw:
                    raise RelayError(409, "Immutable storage entry already differs") from None
            # On replay the unused new temporary remains; successful rename has
            # already removed it atomically, with target nlink=1 throughout.
            with contextlib.suppress(FileNotFoundError):
                os.unlink(temporary, dir_fd=self.fd)
            os.fsync(self.fd)
        finally:
            with contextlib.suppress(FileNotFoundError):
                os.unlink(temporary, dir_fd=self.fd)


class Store:
    def __init__(self, data_dir, *, max_requests=256, max_jobs=256,
                 max_logs_bytes=256 * 1024 * 1024, max_artifacts_bytes=8 * MAX_PACKAGE):
        self.root = SafeDir(data_dir)
        self.path = self.root.path
        self.limits = (max_requests, max_jobs, max_logs_bytes, max_artifacts_bytes)
        for sub in ("artifacts", "logs", "work", "diagnostics"):
            with SafeDir(self.path / sub):
                pass
        dbfd = os.open("relay.sqlite3", os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW,
                       0o600, dir_fd=self.root.fd)
        info = os.fstat(dbfd)
        os.close(dbfd)
        if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1:
            raise ValueError("Database must be a private regular file")
        # Each call has its own connection; thread/process coordination is SQLite.
        with self.db() as db:
            db.execute("PRAGMA journal_mode=WAL")
            db.executescript("""
                CREATE TABLE IF NOT EXISTS requests (
                    id TEXT PRIMARY KEY, transcript TEXT NOT NULL, draft TEXT NOT NULL,
                    parent_id TEXT, parent_revision TEXT, state TEXT NOT NULL,
                    text TEXT, revision TEXT, response_sha TEXT, detail TEXT, created REAL NOT NULL,
                    request_sha TEXT);
                CREATE TABLE IF NOT EXISTS jobs (
                    id TEXT PRIMARY KEY, plan_id TEXT NOT NULL, revision TEXT NOT NULL,
                    idem TEXT UNIQUE NOT NULL, state TEXT NOT NULL, detail TEXT NOT NULL,
                    identity_json TEXT NOT NULL, source_json TEXT, source_sha TEXT,
                    manifest_json TEXT, created REAL NOT NULL, started REAL,
                    UNIQUE(plan_id,revision));
                CREATE TABLE IF NOT EXISTS events (
                    sequence INTEGER PRIMARY KEY AUTOINCREMENT, type TEXT NOT NULL,
                    payload TEXT NOT NULL, created REAL NOT NULL);
                CREATE TABLE IF NOT EXISTS logs (
                    id TEXT PRIMARY KEY, sha TEXT UNIQUE NOT NULL, bytes INTEGER NOT NULL,
                    created REAL NOT NULL);
                CREATE TABLE IF NOT EXISTS diagnostics (
                    id TEXT PRIMARY KEY, sha TEXT NOT NULL, bytes INTEGER NOT NULL);
                CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);
            """)
        # The migration is serialized independently of CREATE IF NOT EXISTS so
        # concurrent API/worker startup cannot both add the same legacy column.
        with self.db(True) as db:
            columns = {row["name"] for row in db.execute("PRAGMA table_info(requests)")}
            if "request_sha" not in columns:
                db.execute("ALTER TABLE requests ADD COLUMN request_sha TEXT")
            job_columns = {row["name"] for row in db.execute("PRAGMA table_info(jobs)")}
            if "inventory_json" not in job_columns:
                db.execute("ALTER TABLE jobs ADD COLUMN inventory_json TEXT")
        try:
            self.reconcile_title_ledger()
        except BaseException:
            self.root.close()
            raise

    def close(self):
        self.root.close()

    @contextlib.contextmanager
    def ledger_lock(self):
        fd = os.open("title-ledger.lock", os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW | os.O_NONBLOCK,
                     0o600, dir_fd=self.root.fd)
        try:
            info = os.fstat(fd)
            if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1:
                raise RelayError(503, "Title ledger lock is not a private regular file")
            fcntl.flock(fd, fcntl.LOCK_EX)
            yield
        finally:
            os.close(fd)

    @staticmethod
    def ledger_entry(value):
        try:
            fields(value, ("job_id", "identity"))
            identifier(value["job_id"], "j")
            identity = value["identity"]
            fields(identity, ("title_id", "content_id", "name", "version"))
            title = identity["title_id"]
            if not isinstance(title, str) or not re.fullmatch(r"PPSA99[0-9]{3}", title) or title == "PPSA99576":
                raise ValueError()
            if not isinstance(identity["content_id"], str) or not re.fullmatch(r"IV0000-" + title + r"_00-[A-Z0-9]{16}", identity["content_id"]):
                raise ValueError()
            name = text(identity["name"], 80, "title name")
            if identity["version"] != "01.000.000" or any(ord(c) < 32 for c in name):
                raise ValueError()
        except (RelayError, KeyError, ValueError, TypeError):
            raise RelayError(503, "Title ledger contains an invalid identity; refusing allocation") from None
        return value

    def read_title_ledger(self, allow_missing=False):
        try:
            raw = self.root.read("title-ledger.json", 512 * 1024)
        except FileNotFoundError:
            if allow_missing:
                return None
            raise RelayError(503, "Title ledger is missing; startup reconciliation is required") from None
        try:
            document = strict_json(raw)
            fields(document, ("schema", "entries", "sha256"))
            if type(document["schema"]) is not int or document["schema"] != 1 or not isinstance(document["entries"], list) or len(document["entries"]) > 999:
                raise ValueError()
            payload = {"schema": 1, "entries": document["entries"]}
            if document["sha256"] != hashlib.sha256(canonical(payload)).hexdigest():
                raise ValueError()
            titles, jobs = set(), set()
            for value in document["entries"]:
                self.ledger_entry(value)
                title = value["identity"]["title_id"]
                if title in titles or value["job_id"] in jobs:
                    raise ValueError()
                titles.add(title)
                jobs.add(value["job_id"])
            if document["entries"] != sorted(document["entries"], key=lambda item: item["identity"]["title_id"]):
                raise ValueError()
            return document["entries"]
        except (RelayError, KeyError, ValueError, TypeError):
            raise RelayError(503, "Title ledger integrity failed; refusing allocation") from None

    def write_title_ledger(self, entries):
        """Under ledger_lock only: fsynced atomic snapshot, before SQLite commit."""
        payload = {"schema": 1, "entries": sorted(entries, key=lambda item: item["identity"]["title_id"])}
        raw = canonical({**payload, "sha256": hashlib.sha256(canonical(payload)).hexdigest()}) + b"\n"
        temporary = ".ledger-" + uuid.uuid4().hex
        fd = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW,
                     0o600, dir_fd=self.root.fd)
        try:
            with os.fdopen(fd, "wb") as stream:
                stream.write(raw)
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(temporary, "title-ledger.json", src_dir_fd=self.root.fd, dst_dir_fd=self.root.fd)
            os.fsync(self.root.fd)
        finally:
            with contextlib.suppress(FileNotFoundError):
                os.unlink(temporary, dir_fd=self.root.fd)

    def reconcile_title_ledger(self):
        # Do not hold a ledger lock while waiting for a SQLite write lock:
        # approve() holds the DB write lock before reserving its title.
        with self.db() as db:
            rows = db.execute("SELECT id,identity_json FROM jobs").fetchall()
        historical = []
        try:
            for row in rows:
                historical.append(self.ledger_entry({"job_id": row["id"], "identity": strict_json(row["identity_json"].encode())}))
        except RelayError:
            raise RelayError(503, "Historical job identity is corrupt; refusing ledger recovery") from None
        with self.ledger_lock():
            entries = self.read_title_ledger(allow_missing=True)
            changed = entries is None
            entries = entries or []
            by_title = {entry["identity"]["title_id"]: entry for entry in entries}
            by_job = {entry["job_id"]: entry for entry in entries}
            for entry in historical:
                old = by_title.get(entry["identity"]["title_id"])
                same_job = by_job.get(entry["job_id"])
                if (old and old != entry) or (same_job and same_job != entry):
                    raise RelayError(503, "Historical job conflicts with title ledger; refusing startup")
                if not old:
                    entries.append(entry)
                    by_title[entry["identity"]["title_id"]] = entry
                    by_job[entry["job_id"]] = entry
                    changed = True
            if changed:
                self.write_title_ledger(entries)

    def reserve_identity(self, job_id, inventory):
        identifier(job_id, "j")
        snapshot = console_inventory(inventory)
        with self.ledger_lock():
            entries = self.read_title_ledger()
            if any(entry["job_id"] == job_id for entry in entries):
                raise RelayError(409, "Job identity is already permanently reserved")
            used = RESERVED_TITLES | set(snapshot["title_ids"]) | {entry["identity"]["title_id"] for entry in entries}
            title = next((f"PPSA99{i:03d}" for i in range(1000)
                          if f"PPSA99{i:03d}" not in used), None)
            if title is None:
                raise RelayError(507, "No unused homebrew title identities remain")
            identity = {"title_id": title, "content_id": f"IV0000-{title}_00-MUSE{uuid.uuid4().hex[:12].upper()}",
                        "name": f"Muse App {title[-3:]}", "version": "01.000.000"}
            entries.append({"job_id": job_id, "identity": identity})
            # A failed later SQLite commit burns this reservation intentionally.
            # Neither DB deletion nor retry may recycle an installed title ID.
            self.write_title_ledger(entries)
            return identity

    @contextlib.contextmanager
    def db(self, write=False):
        db = sqlite3.connect(str(self.path / "relay.sqlite3"), timeout=15, isolation_level=None)
        db.row_factory = sqlite3.Row
        db.execute("PRAGMA synchronous=FULL")
        db.execute("PRAGMA foreign_keys=ON")
        try:
            if write:
                db.execute("BEGIN IMMEDIATE")
            yield db
            if write:
                db.execute("COMMIT")
        except BaseException:
            if write and db.in_transaction:
                db.execute("ROLLBACK")
            raise
        finally:
            db.close()

    @staticmethod
    def event(db, kind, value):
        db.execute("INSERT INTO events(type,payload,created) VALUES(?,?,?)",
                   (kind, canonical(value).decode(), time.time()))

    @staticmethod
    def row(db, table, id):
        if table not in ("requests", "jobs", "logs"):
            raise ValueError("Bad table")
        row = db.execute(f"SELECT * FROM {table} WHERE id=?", (id,)).fetchone()
        if row is None:
            raise RelayError(404, "Unknown relay identifier")
        return row

    def submit(self, value):
        fields(value, ("transcript", "draft", "source"), ("parent_plan_id", "parent_revision", "client_request_id"))
        transcript = text(value["transcript"], 4096, "transcript")
        draft = text(value["draft"], 4096, "draft", empty=True)
        if value["source"] != "on_console":
            raise RelayError(400, "Expected on_console transcript source")
        parent = value.get("parent_plan_id")
        revision = value.get("parent_revision")
        if (parent is None) != (revision is None):
            raise RelayError(400, "Parent id and revision must be supplied together")
        request_id = identifier(value["client_request_id"], "p") if "client_request_id" in value else "p" + uuid.uuid4().hex
        request_sha = hashlib.sha256(canonical({key: item for key, item in value.items() if key != "client_request_id"})).hexdigest()
        with self.db(True) as db:
            existing = db.execute("SELECT * FROM requests WHERE id=?", (request_id,)).fetchone()
            if existing:
                previous_sha = existing["request_sha"]
                if previous_sha is None:
                    # Legacy requests predate this field; their persisted input
                    # fields are sufficient to derive the same fingerprint.
                    previous = {"transcript": existing["transcript"], "draft": existing["draft"], "source": "on_console"}
                    if existing["parent_id"] is not None:
                        previous.update(parent_plan_id=existing["parent_id"], parent_revision=existing["parent_revision"])
                    previous_sha = hashlib.sha256(canonical(previous)).hexdigest()
                if previous_sha != request_sha:
                    raise RelayError(409, "Client request id already belongs to different request content")
                return {"transcript": existing["transcript"], "plan_id": request_id}
            if db.execute("SELECT count(*) FROM requests").fetchone()[0] >= self.limits[0]:
                raise RelayError(507, "Request quota reached; operator archival is required")
            parent_text = None
            if parent is not None:
                identifier(parent, "p")
                old = self.row(db, "requests", parent)
                if old["state"] != "ready" or old["revision"] != revision:
                    raise RelayError(409, "Parent plan is pending or its revision does not match")
                parent_text = old["text"]
            db.execute("INSERT INTO requests(id,transcript,draft,parent_id,parent_revision,state,created,request_sha) VALUES(?,?,?,?,?,'planning',?,?)",
                       (request_id, transcript, draft, parent, revision, time.time(), request_sha))
            payload = {"schema": 1, "type": "request", "request_id": request_id,
                       "transcript": transcript, "draft": draft}
            if parent:
                payload.update(parent_plan_id=parent, parent_revision=revision, parent_text=parent_text)
            self.event(db, "request", payload)
        return {"transcript": transcript, "plan_id": request_id}

    def plan(self, id):
        identifier(id, "p")
        with self.db() as db:
            row = self.row(db, "requests", id)
        if row["state"] == "planning":
            return 202, {"id": id, "state": "planning", "detail": "Waiting for a Muse plan through the configured exchange"}
        if row["state"] == "error":
            raise RelayError(422, row["detail"])
        return 200, {"id": id, "revision": row["revision"], "text": row["text"]}

    def ingest_plan(self, value):
        failure = value.get("type") == "plan_error" if isinstance(value, dict) else False
        fields(value, ("schema", "type", "request_id", "detail" if failure else "text"))
        if type(value["schema"]) is not int or value["schema"] != 1 or value["type"] not in ("plan", "plan_error"):
            raise RelayError(400, "Expected schema-1 plan or plan_error")
        id = identifier(value["request_id"], "p")
        content = text(value["detail" if failure else "text"], 1023 if failure else 4096, "plan response")
        digest = hashlib.sha256(canonical(value)).hexdigest()
        with self.db(True) as db:
            row = self.row(db, "requests", id)
            if row["response_sha"]:
                if row["response_sha"] != digest:
                    raise RelayError(409, "Plan response is immutable; submit a new revision request")
            else:
                revision = hashlib.sha256(canonical({"request_id": id, "transcript": row["transcript"],
                    "parent_plan_id": row["parent_id"], "parent_revision": row["parent_revision"], "text": content})).hexdigest()
                db.execute("UPDATE requests SET state=?,text=?,revision=?,response_sha=?,detail=? WHERE id=?",
                           ("error" if failure else "ready", None if failure else content,
                            None if failure else revision, digest, content if failure else None, id))
        return {"accepted": True, "request_id": id}

    def approve(self, id, value):
        identifier(id, "p")
        fields(value, ("revision", "idempotency_key", "artifact_kind"), ("inventory",))
        revision = value["revision"]
        if not isinstance(revision, str) or not SHA.fullmatch(revision) or value["artifact_kind"] != "pkg":
            raise RelayError(400, "Approval requires exact revision and pkg artifact kind")
        idem = text(value["idempotency_key"], 257, "idempotency key")
        if not re.fullmatch(r"[A-Za-z0-9_:-]+", idem):
            raise RelayError(400, "Invalid idempotency key")
        with self.db(True) as db:
            plan = self.row(db, "requests", id)
            if plan["state"] != "ready" or plan["revision"] != revision:
                raise RelayError(409, "Plan is pending, failed, or approval revision is stale")
            old = db.execute("SELECT * FROM jobs WHERE idem=?", (idem,)).fetchone()
            if old and (old["plan_id"] != id or old["revision"] != revision):
                raise RelayError(409, "Idempotency key already belongs to another approval")
            old = old or db.execute("SELECT * FROM jobs WHERE plan_id=? AND revision=?", (id, revision)).fetchone()
            if old:
                return {"job_id": old["id"]}
            if "inventory" not in value:
                raise RelayError(428, "Read ShadowMountPlus inventory on the console before approving; update MuseStation if needed")
            inventory = console_inventory(value["inventory"])
            if db.execute("SELECT count(*) FROM jobs").fetchone()[0] >= self.limits[1]:
                raise RelayError(507, "Build quota reached; operator archival is required")
            job = "j" + uuid.uuid4().hex
            identity = self.reserve_identity(job, inventory)
            db.execute("INSERT INTO jobs(id,plan_id,revision,idem,state,detail,identity_json,created,inventory_json) VALUES(?,?,?,?,'awaiting_source',?,?,?,?)",
                       (job, id, revision, idem, "Approved; waiting for Muse source", canonical(identity).decode(), time.time(), canonical(inventory).decode()))
            self.event(db, "approved_job", {"schema": 1, "type": "approved_job", "job_id": job,
                "request_id": id, "approved_revision": revision, "transcript": plan["transcript"],
                "text": plan["text"], "artifact_kind": "pkg", "identity": identity,
                "max_attempts": 1})
        return {"job_id": job}

    def ingest_source(self, value):
        digest = source_manifest(value)
        id = value["job_id"]
        with self.db(True) as db:
            row = self.row(db, "jobs", id)
            if value["approved_revision"] != row["revision"]:
                raise RelayError(409, "Source does not match the approved revision")
            if row["source_sha"]:
                if row["source_sha"] != digest:
                    raise RelayError(409, "Source is immutable; a failed build requires a new approved request")
            elif row["state"] != "awaiting_source":
                raise RelayError(409, "Job no longer accepts source")
            else:
                db.execute("UPDATE jobs SET source_json=?,source_sha=?,state='queued',detail='Source accepted; waiting for compiler worker' WHERE id=?",
                           (canonical(value).decode(), digest, id))
        return {"accepted": True, "job_id": id, "source_sha256": digest}

    def status(self, id):
        identifier(id, "j")
        with self.db() as db:
            row = self.row(db, "jobs", id)
        if row["state"] == "done":
            return json.loads(row["manifest_json"])
        return {"state": "error" if row["state"] == "error" else "building", "detail": row["detail"]}

    def outbox(self, after=0, limit=32, byte_limit=256 * 1024):
        if type(after) is not int or after < 0 or after > 2**63 - 1:
            raise RelayError(400, "Invalid outbox cursor")
        with self.db() as db:
            rows = db.execute("SELECT * FROM events WHERE sequence>? ORDER BY sequence LIMIT ?", (after, min(max(limit, 1), 64))).fetchall()
        events, total = [], 0
        for row in rows:
            event = {"sequence": row["sequence"], "type": row["type"], "payload": json.loads(row["payload"])}
            size = len(canonical(event))
            if events and total + size > byte_limit:
                break
            total += size
            events.append(event)
        return {"schema": 1, "events": events, "next_after": events[-1]["sequence"] if events else after}

    def append_logs(self, raw):
        if not raw or len(raw) > MAX_LOG:
            raise RelayError(413, "Log body must contain 1 byte to 8 MiB")
        try:
            lines = raw.splitlines()
            if len(lines) > 65536:
                raise RelayError(413, "Too many log records")
            for line in lines:
                if not line or len(line) > 65536:
                    raise RelayError(400, "Log lines must be nonempty and at most 64 KiB")
                entry = strict_json(line)
                for key in ("ts", "level", "event", "detail"):
                    if key not in entry:
                        raise RelayError(400, "Log record lacks ts, level, event, or detail")
                for key in ("level", "event", "detail"):
                    text(entry[key], 65536, f"log {key}", empty=key == "detail")
        except (ValueError, UnicodeError):
            raise RelayError(400, "Invalid structured JSONL log") from None
        digest = hashlib.sha256(raw).hexdigest()
        with self.db(True) as db:
            old = db.execute("SELECT id FROM logs WHERE sha=?", (digest,)).fetchone()
            if old:
                return {"accepted": True, "log_id": old[0]}
            count, total = db.execute("SELECT count(*),coalesce(sum(bytes),0) FROM logs").fetchone()
            if count >= 4096 or total + len(raw) > self.limits[2]:
                raise RelayError(507, "Log quota reached; operator archival is required")
            # Content-derived id makes a crash between file fsync and DB commit
            # replay the same file, rather than accumulating orphaned logs.
            id = "l" + digest[:32]
            with SafeDir(self.path / "logs") as directory:
                directory.write(id + ".jsonl", raw)
            db.execute("INSERT INTO logs VALUES(?,?,?,?)", (id, digest, len(raw), time.time()))
            self.event(db, "logs", {"schema": 1, "type": "logs", "log_id": id,
                                   "size": len(raw), "sha256": digest})
        return {"accepted": True, "log_id": id}

    def log_bytes(self, id):
        identifier(id, "l")
        with self.db() as db:
            row = self.row(db, "logs", id)
        with SafeDir(self.path / "logs") as directory:
            raw = directory.read(id + ".jsonl", MAX_LOG)
        if len(raw) != row["bytes"] or hashlib.sha256(raw).hexdigest() != row["sha"]:
            raise RelayError(500, "Stored log integrity check failed")
        return raw

    def claim_job(self):
        with self.db(True) as db:
            # Globally only one compilation can be active, including other workers.
            if db.execute("SELECT 1 FROM jobs WHERE state='building'").fetchone():
                return None
            row = db.execute("SELECT * FROM jobs WHERE state='queued' ORDER BY created,id LIMIT 1").fetchone()
            if row is None:
                return None
            db.execute("UPDATE jobs SET state='building',detail='Compiling approved source',started=? WHERE id=?", (time.time(), row["id"]))
            return {"id": row["id"], "revision": row["revision"],
                    "source_manifest": json.loads(row["source_json"]), "identity": json.loads(row["identity_json"])}

    def fail_job(self, id, detail):
        identifier(id, "j")
        detail = text(detail, 1023, "build failure")
        with self.db(True) as db:
            row = self.row(db, "jobs", id)
            if row["state"] in ("done", "error"):
                return
            db.execute("UPDATE jobs SET state='error',detail=? WHERE id=?", (detail, id))
            diagnostics = self.diagnostics_reference(db, id)
            self.event(db, "result", {"schema": 1, "type": "result", "job_id": id,
                "approved_revision": row["revision"], "state": "error", "detail": detail,
                "attempts": 1, "retry_requires_new_approval": True, "diagnostics": diagnostics})

    @staticmethod
    def diagnostics_reference(db, id):
        row = db.execute("SELECT * FROM diagnostics WHERE id=?", (id,)).fetchone()
        return {"job_id": id, "size": row["bytes"], "sha256": row["sha"]} if row else None

    def save_diagnostics(self, id, value):
        identifier(id, "j")
        raw = canonical({"schema": 1, "type": "diagnostics", "job_id": id, "files": value}) + b"\n"
        if len(raw) > 256 * 1024:
            raise RelayError(413, "Diagnostics exceed 256 KiB")
        with self.db(True) as db:
            self.row(db, "jobs", id)
            with SafeDir(self.path / "diagnostics") as directory:
                directory.write(id + ".json", raw)
            digest = hashlib.sha256(raw).hexdigest()
            old = db.execute("SELECT sha FROM diagnostics WHERE id=?", (id,)).fetchone()
            if old and old[0] != digest:
                raise RelayError(409, "Build diagnostics are immutable")
            db.execute("INSERT OR IGNORE INTO diagnostics VALUES(?,?,?)", (id, digest, len(raw)))

    def diagnostic_bytes(self, id):
        identifier(id, "j")
        with self.db() as db:
            row = db.execute("SELECT * FROM diagnostics WHERE id=?", (id,)).fetchone()
        if row is None:
            raise RelayError(404, "No build diagnostics")
        with SafeDir(self.path / "diagnostics") as directory:
            raw = directory.read(id + ".json", 256 * 1024)
        if len(raw) != row["bytes"] or hashlib.sha256(raw).hexdigest() != row["sha"]:
            raise RelayError(500, "Stored diagnostics integrity check failed")
        return raw

    def worker_heartbeat(self):
        with self.db(True) as db:
            db.execute("INSERT INTO meta(key,value) VALUES('worker_heartbeat',?) ON CONFLICT(key) DO UPDATE SET value=excluded.value", (str(time.time()),))

    def worker_state(self):
        with self.db() as db:
            row = db.execute("SELECT value FROM meta WHERE key='worker_heartbeat'").fetchone()
            active = db.execute("SELECT id FROM jobs WHERE state='building'").fetchone()
        seen = float(row[0]) if row else None
        return {"last_heartbeat": seen, "recent_heartbeat": seen is not None and time.time() - seen < 30,
                "active_job": active[0] if active else None, "hardware_verified": False}

    def recover_interrupted(self):
        """Worker startup, under its exclusive process lock. Never silently rebuild."""
        with self.db() as db:
            ids = [row[0] for row in db.execute("SELECT id FROM jobs WHERE state='building'")]
        for id in ids:
            self.fail_job(id, "Compiler worker interrupted; this attempt is not automatically repeated. Submit a new request for review and approval.")
        return len(ids)

    def finish_job(self, id, result):
        identifier(id, "j")
        with self.db() as db:
            row = self.row(db, "jobs", id)
        if row["state"] != "building":
            raise RelayError(409, "Job is not the active build")
        identity = json.loads(row["identity_json"])
        if any(result.get(key) != identity[key] for key in ("title_id", "content_id", "version")):
            raise RelayError(400, "Builder returned an unassigned package identity")
        size, digest = result.get("bytes"), result.get("sha256")
        if type(size) is not int or not 0x11000 <= size <= MAX_PACKAGE or not isinstance(digest, str) or not SHA.fullmatch(digest):
            raise RelayError(400, "Builder returned an invalid package manifest")
        out = self.path / "work" / id / "out"
        artifact = Path(result.get("artifact_path", ""))
        if not artifact.is_absolute() or artifact.parent != out or artifact.name in ("", ".", ".."):
            raise RelayError(400, "Builder artifact must be a direct file in the assigned output directory")
        with self.db(True) as db:
            current = self.row(db, "jobs", id)
            if current["state"] != "building":
                raise RelayError(409, "Job is not active")
            total = sum(json.loads(r[0])["size"] for r in db.execute("SELECT manifest_json FROM jobs WHERE state='done'"))
            if total + size > self.limits[3]:
                raise RelayError(507, "Artifact quota reached; operator archival is required")
            with SafeDir(out, False) as source, SafeDir(self.path / "artifacts") as dest:
                src = source.open(artifact.name)
                temporary = ".tmp-" + uuid.uuid4().hex
                fd = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600, dir_fd=dest.fd)
                try:
                    with os.fdopen(src, "rb") as inp, os.fdopen(fd, "wb") as target:
                        if os.fstat(inp.fileno()).st_size != size:
                            raise RelayError(400, "Builder artifact length mismatch")
                        h = inp.read(0x100)
                        pfs, extent, meta = struct.unpack_from("<QQ", h, 0x10) + (struct.unpack_from("<Q", h, 0x58)[0],) if len(h) == 0x100 else (0, 0, 0)
                        if h[:7] != b"\x7fFIH\x01\x00\x03" or pfs != 0x10000 or not extent or extent > size - pfs or meta != pfs + extent or meta > size - 0x80:
                            raise RelayError(400, "Builder artifact is not a bounded debug FIH package")
                        inp.seek(meta)
                        cnt = inp.read(0x80)
                        if cnt[:4] != b"\x7fCNT" or cnt[0x40:0x65] != identity["content_id"].encode() + b"\0":
                            raise RelayError(400, "Builder package content identity mismatch")
                        inp.seek(0)
                        hash = hashlib.sha256()
                        copied = 0
                        while chunk := inp.read(65536):
                            copied += len(chunk)
                            if copied > size:
                                raise RelayError(400, "Builder artifact grew while copying")
                            hash.update(chunk)
                            target.write(chunk)
                        if copied != size or hash.hexdigest() != digest:
                            raise RelayError(400, "Builder artifact SHA256 mismatch")
                        target.flush()
                        os.fsync(target.fileno())
                    rename_noreplace(dest.fd, temporary, id + ".pkg")
                    os.fsync(dest.fd)
                finally:
                    with contextlib.suppress(FileNotFoundError):
                        os.unlink(temporary, dir_fd=dest.fd)
            manifest = {"state": "done", "kind": "pkg", "size": size, "sha256": digest,
                        "title_id": identity["title_id"], "content_id": identity["content_id"]}
            db.execute("UPDATE jobs SET state='done',detail='Compile and package verification completed; hardware verification pending',manifest_json=? WHERE id=?", (canonical(manifest).decode(), id))
            self.event(db, "result", {"schema": 1, "type": "result", "job_id": id,
                "approved_revision": row["revision"], **manifest, "source_sha256": row["source_sha"],
                "hardware_verified": False, "diagnostics": self.diagnostics_reference(db, id)})
        return manifest

    def artifact(self, id):
        manifest = self.status(id)
        if manifest["state"] != "done":
            raise RelayError(409, "Artifact is not ready")
        with SafeDir(self.path / "artifacts") as directory:
            fd = directory.open(id + ".pkg")
        if os.fstat(fd).st_size != manifest["size"]:
            os.close(fd)
            raise RelayError(500, "Stored package length mismatch")
        return fd, manifest

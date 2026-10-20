import concurrent.futures
import hashlib
import http.client
import json
import os
from pathlib import Path
import socket
import sqlite3
import struct
import tempfile
import threading
import unittest
from unittest.mock import patch

from relay.exchange import Exchange
from relay.server import Application, RelayHTTPServer, main as server_main, read_console_token, read_token
from relay.store import RelayError, SafeDir, Store, canonical, rename_noreplace, strict_json
from relay.worker import builder_for_store, recover_jobs, run_one


def inventory(*titles):
    return {"source": "shadowmountplus", "api_version": 1, "title_ids": list(titles)}


def fixture_package(path, identity):
    """Structural FIH fixture only, never advertised as an installable package."""
    raw = bytearray(0x12000)
    raw[:7] = b"\x7fFIH\x01\x00\x03"
    struct.pack_into("<QQ", raw, 0x10, 0x10000, 0x1000)
    struct.pack_into("<Q", raw, 0x58, 0x11000)
    raw[0x11000:0x11004] = b"\x7fCNT"
    raw[0x11040:0x11064] = identity["content_id"].encode()
    path.write_bytes(raw)
    return {"artifact_path": str(path), "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest(),
            **{k: identity[k] for k in ("title_id", "content_id", "version")}}


class StoreCase(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.base = Path(self.temp.name)
        self.store = Store(self.base / "data")
        self.exchange = Exchange(self.store, self.base / "exchange")

    def tearDown(self):
        self.store.close()
        self.temp.cleanup()

    def pending(self, **kw):
        return self.store.submit({"transcript": "Make a counter", "draft": "", "source": "on_console", **kw})["plan_id"]

    def ready(self):
        id = self.pending()
        self.store.ingest_plan({"schema": 1, "type": "plan", "request_id": id, "text": "Build a counter. Cross adds one."})
        return id, self.store.plan(id)[1]["revision"]

    def approved(self):
        id, revision = self.ready()
        job = self.store.approve(id, {"revision": revision, "idempotency_key": id + ":" + revision, "artifact_kind": "pkg", "inventory": inventory()})["job_id"]
        return job, revision

    def source(self, job, revision):
        return {"schema": 1, "type": "source", "job_id": job, "approved_revision": revision,
                "language": "cpp", "files": {"main.cpp": "int main(){return 0;}"}}

    def error(self, status, fn, *args):
        with self.assertRaises(RelayError) as caught:
            fn(*args)
        self.assertEqual(caught.exception.status, status)

    def test_pending_never_approves_or_claims(self):
        id = self.pending()
        status, plan = self.store.plan(id)
        self.assertEqual(status, 202)
        self.assertNotIn("revision", plan)
        self.error(409, self.store.approve, id, {"revision": "0" * 64, "idempotency_key": "one", "artifact_kind": "pkg", "inventory": inventory()})
        self.assertIsNone(self.store.claim_job())
        self.assertEqual(self.store.outbox()["events"][0]["payload"]["request_id"], id)

    def test_immutable_plan_and_revision_context(self):
        id, revision = self.ready()
        same = {"schema": 1, "type": "plan", "request_id": id, "text": "Build a counter. Cross adds one."}
        self.assertTrue(self.store.ingest_plan(same)["accepted"])
        self.error(409, self.store.ingest_plan, {**same, "text": "Changed after approval"})
        with self.assertRaises(RelayError):
            self.pending(parent_plan_id=id, parent_revision="stale")
        new = self.pending(parent_plan_id=id, parent_revision=revision)
        event = self.store.outbox()["events"][-1]["payload"]
        self.assertEqual(event["parent_text"], same["text"])
        self.assertNotEqual(id, new)
        self.assertEqual(self.store.plan(id)[1]["revision"], revision)

    def test_concurrent_approvals_exactly_one_job(self):
        id, revision = self.ready()
        body = {"revision": revision, "idempotency_key": id + ":" + revision, "artifact_kind": "pkg", "inventory": inventory()}
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
            jobs = list(pool.map(lambda _: self.store.approve(id, body)["job_id"], range(16)))
        self.assertEqual(len(set(jobs)), 1)
        self.assertEqual(self.store.approve(id, {**body, "idempotency_key": "another-key"})["job_id"], jobs[0])
        self.error(409, self.store.approve, id, {**body, "revision": "a" * 64})
        second, rev2 = self.ready()
        self.error(409, self.store.approve, second, {**body, "revision": rev2})
        self.assertEqual(sum(e["type"] == "approved_job" for e in self.store.outbox()["events"]), 1)

    def test_source_bound_to_approval_and_no_scripts(self):
        job, revision = self.approved()
        value = self.source(job, revision)
        self.error(409, self.store.ingest_source, {**value, "approved_revision": "b" * 64})
        for key in ("recipe", "env", "title_id"):
            self.error(400, self.store.ingest_source, {**value, key: "ignored?"})
        for name in ("../main.cpp", "include/../x.hpp", "/tmp/x.hpp", "include//x.hpp", "build.sh", "src/a.cpp"):
            self.error(400, self.store.ingest_source, {**value, "files": {"main.cpp": "ok", name: "bad"}})
        self.error(400, self.store.ingest_source, {**value, "files": {"main.cpp": "a" * (256 * 1024 + 1)}})
        self.store.ingest_source(value)
        self.store.ingest_source(value)
        self.error(409, self.store.ingest_source, {**value, "files": {"main.cpp": "different"}})
        claim = self.store.claim_job()
        self.assertEqual(claim["revision"], revision)
        self.assertNotEqual(claim["identity"]["title_id"], "PPSA99576")
        self.assertIsNone(self.store.claim_job())

    def test_restart_recovers_pending_and_fails_interrupted_once(self):
        pending = self.pending()
        job, revision = self.approved()
        self.store.ingest_source(self.source(job, revision))
        self.store.claim_job()
        self.store.close()
        self.store = Store(self.base / "data")
        self.assertEqual(self.store.plan(pending)[0], 202)
        self.assertEqual(self.store.recover_interrupted(), 1)
        self.assertEqual(self.store.recover_interrupted(), 0)
        self.assertEqual(self.store.status(job)["state"], "error")
        self.assertIsNone(self.store.claim_job())
        self.store.ingest_source(self.source(job, revision))  # replay, not a retry
        self.assertIsNone(self.store.claim_job())

    def test_worker_result_artifact_exact_bytes_and_durable_export(self):
        job, revision = self.approved()
        self.store.ingest_source(self.source(job, revision))
        class BuilderFixture:
            def build(inner, job_dir, source, identity, output_dir):
                self.assertEqual(source["approved_revision"], revision)
                return fixture_package(output_dir / "title.pkg", identity)
        self.assertTrue(run_one(self.store, BuilderFixture()))
        manifest = self.store.status(job)
        self.assertEqual(manifest["state"], "done")
        fd, read_manifest = self.store.artifact(job)
        with os.fdopen(fd, "rb") as stream:
            raw = stream.read()
        self.assertEqual(hashlib.sha256(raw).hexdigest(), manifest["sha256"])
        self.assertEqual(len(raw), manifest["size"])
        self.assertEqual(manifest, read_manifest)
        self.assertTrue(self.exchange.tick())
        result = json.loads((self.exchange.path / "outbound/results" / (job + ".json")).read_bytes())
        self.assertFalse(result["hardware_verified"])
        # Restart export cursor and replay every event; immutable writes converge.
        with self.store.db(True) as db:
            db.execute("DELETE FROM meta WHERE key='exchange_export_after'")
        self.assertTrue(self.exchange.tick())

    def test_worker_rejects_mismatched_identity_and_hash(self):
        for tamper in ("sha256", "title_id", "artifact_path"):
            job, revision = self.approved()
            self.store.ingest_source(self.source(job, revision))
            class BadBuilder:
                def build(inner, job_dir, source, identity, output_dir):
                    result = fixture_package(output_dir / "title.pkg", identity)
                    result[tamper] = "0" * 64 if tamper == "sha256" else "PPSA99576" if tamper == "title_id" else "/tmp/foreign.pkg"
                    return result
            run_one(self.store, BadBuilder())
            self.assertEqual(self.store.status(job)["state"], "error")
            self.error(409, self.store.artifact, job)

    def test_atomic_folder_partial_malformed_symlink_and_duplicate(self):
        id = self.pending()
        incoming = self.exchange.path / "inbound/plans"
        value = {"schema": 1, "type": "plan", "request_id": id, "text": "Build a counter."}
        part = incoming / (id + ".part")
        part.write_bytes(canonical(value))
        self.assertTrue(self.exchange.tick())
        self.assertEqual(self.store.plan(id)[0], 202)
        part.rename(incoming / (id + ".json"))
        self.assertTrue(self.exchange.tick())
        self.assertEqual(self.store.plan(id)[0], 200)
        self.assertTrue(self.exchange.tick())
        bad = self.pending()
        (incoming / (bad + ".json")).write_bytes(b'{"schema":1')
        link = self.pending()
        secret = self.base / "secret"
        secret.write_bytes(canonical({**value, "request_id": link}))
        (incoming / (link + ".json")).symlink_to(secret)
        self.assertTrue(self.exchange.tick())
        self.assertEqual(self.store.plan(bad)[0], 202)
        self.assertEqual(self.store.plan(link)[0], 202)
        for request in (bad, link):
            receipt = json.loads((self.exchange.path / "receipts/plans" / (request + ".json")).read_bytes())
            self.assertFalse(receipt["accepted"])

    def test_json_parser_and_private_paths(self):
        for raw in (b'{"x":1,"x":2}', b'{"x":NaN}', b'{"x":"\\ud800"}', b'[]', b'\xff'):
            self.error(400, strict_json, raw)
        self.error(400, self.store.ingest_plan, {"schema": True, "type": "plan", "request_id": self.pending(), "text": "bad"})
        target = self.base / "target"
        target.mkdir()
        link = self.base / "link"
        link.symlink_to(target)
        with self.assertRaises(OSError):
            SafeDir(link)
        with SafeDir(target) as directory:
            directory.write("entry", b"data")
            directory.write("entry", b"data")
            self.error(409, directory.write, "entry", b"changed")
            (target / "alias").symlink_to("entry")
            with self.assertRaises(OSError):
                directory.read("alias", 20)
            os.mkfifo(target / "pipe")
            self.error(400, directory.read, "pipe", 20)

    def test_logs_exact_durable_idempotent_and_quota(self):
        raw = b'{"ts":123,"level":"error","event":"launch","detail":"failed"}\n'
        a = self.store.append_logs(raw)
        self.assertEqual(a, self.store.append_logs(raw))
        self.assertEqual(self.store.log_bytes(a["log_id"]), raw)
        self.assertTrue(self.exchange.tick())
        self.assertEqual((self.exchange.path / "outbound/logs" / (a["log_id"] + ".jsonl")).read_bytes(), raw)
        self.error(400, self.store.append_logs, b'{"message":"unstructured"}\n')
        self.store.limits = (256, 256, len(raw), 8 * 1024**3)
        self.error(507, self.store.append_logs, raw.replace(b"failed", b"another"))

    def test_bounded_request_job_outbox(self):
        self.store.limits = (2, 1, 256 * 1024**2, 8 * 1024**3)
        first, rev = self.approved()
        second, revision = self.ready()
        self.error(507, self.pending)
        self.error(507, self.store.approve, second, {"revision": revision, "idempotency_key": "second", "artifact_kind": "pkg", "inventory": inventory()})
        page = self.store.outbox(0, 1)
        self.assertEqual(len(page["events"]), 1)
        nextpage = self.store.outbox(page["next_after"])
        self.assertGreater(nextpage["events"][0]["sequence"], page["next_after"])
        self.error(400, self.store.outbox, -1)

    def test_compile_failure_exports_real_bounded_diagnostics(self):
        job, revision = self.approved()
        self.store.ingest_source(self.source(job, revision))
        class Failure(Exception):
            pass
        class BuilderFixture:
            def build(inner, job_dir, source, identity, output_dir):
                log = job_dir / "compile.log"
                log.write_bytes(b"x" * 30000 + b"\nmain.cpp:12: error: undeclared identifier 'bad'\n")
                failure = Failure("Native source compile failed")
                failure.message = str(failure)
                failure.log_paths = [log]
                raise failure
        self.assertTrue(run_one(self.store, BuilderFixture()))
        self.assertEqual(self.store.status(job)["state"], "error")
        diagnostic = json.loads(self.store.diagnostic_bytes(job))
        self.assertTrue(diagnostic["files"][0]["truncated"])
        self.assertIn("undeclared identifier", diagnostic["files"][0]["tail"])
        event = self.store.outbox()["events"][-1]["payload"]
        self.assertEqual(event["diagnostics"]["job_id"], job)
        self.assertTrue(self.exchange.tick())
        self.assertEqual((self.exchange.path / "outbound/results" / (job + ".diagnostics.json")).read_bytes(), self.store.diagnostic_bytes(job))
        self.assertFalse(self.store.worker_state()["recent_heartbeat"])
        self.store.worker_heartbeat()
        self.assertTrue(self.store.worker_state()["recent_heartbeat"])

    def test_completed_job_survives_private_cleanup_error(self):
        job, revision = self.approved()
        self.store.ingest_source(self.source(job, revision))
        class BuilderFixture:
            def build(inner, job_dir, source, identity, output_dir):
                return fixture_package(output_dir / "title.pkg", identity)
        unlink = os.unlink
        def cleanup_failure(path, *args, **kwargs):
            if path == "title.pkg":
                raise PermissionError("fixture denied redundant cleanup")
            return unlink(path, *args, **kwargs)
        with patch("relay.worker.os.unlink", side_effect=cleanup_failure):
            self.assertTrue(run_one(self.store, BuilderFixture()))
        self.assertEqual(self.store.status(job)["state"], "done")
        fd, manifest = self.store.artifact(job)
        os.close(fd)
        self.assertEqual(manifest["state"], "done")
        self.assertEqual(self.store.outbox()["events"][-1]["payload"]["state"], "done")

    def test_interrupted_job_exports_existing_diagnostics_before_error(self):
        job, revision = self.approved()
        self.store.ingest_source(self.source(job, revision))
        self.store.claim_job()
        root = self.store.path / "work" / job
        root.mkdir()
        (root / "compiler.log").write_text("compiler was interrupted after this diagnostic\n")
        self.assertEqual(recover_jobs(self.store), 1)
        self.assertEqual(self.store.status(job)["state"], "error")
        result = self.store.outbox()["events"][-1]["payload"]
        self.assertEqual(result["diagnostics"]["job_id"], job)
        self.assertIn("compiler was interrupted", self.store.diagnostic_bytes(job).decode())

    def test_exchange_retries_transient_database_failure(self):
        with patch.object(self.exchange, "_export", side_effect=sqlite3.OperationalError("database is locked")):
            self.assertFalse(self.exchange.tick())
        self.assertIn("database temporarily unavailable", self.exchange.last_error)
        self.assertTrue(self.exchange.tick())
        self.assertEqual(self.exchange.last_error, "")
        self.assertIsNotNone(self.exchange.last_success)

    def test_worker_forwards_distinct_private_store_namespaces(self):
        second = Store(self.base / "other-data")
        try:
            first_builder = builder_for_store(self.store, self.base / "common-kit", "compiler", "package")
            other_builder = builder_for_store(second, self.base / "common-kit", "compiler", "package")
            self.assertEqual(first_builder.namespace_root, self.store.path)
            self.assertEqual(other_builder.namespace_root, second.path)
            self.assertNotEqual(first_builder.container_name, other_builder.container_name)
            self.assertNotEqual(first_builder.namespace_hash, other_builder.namespace_hash)
        finally:
            second.close()

    def test_immutable_publish_crash_has_no_alias_and_replays(self):
        root = self.base / "publication"
        class InterruptedPublication(BaseException):
            pass
        def publish_then_crash(fd, temporary, name):
            rename_noreplace(fd, temporary, name)
            raise InterruptedPublication()
        with SafeDir(root) as directory:
            # Simulate termination immediately after atomic publish, before any
            # later cleanup/directory fsync. Even at this point target has one name.
            with patch("relay.store.rename_noreplace", side_effect=publish_then_crash), patch("relay.store.os.link", side_effect=AssertionError("publication must never hard-link")):
                with self.assertRaises(InterruptedPublication):
                    directory.write("committed", b"durable bytes")
            self.assertEqual((root / "committed").stat().st_nlink, 1)
            self.assertEqual(list(root.iterdir()), [root / "committed"])
            directory.write("committed", b"durable bytes")
            self.error(409, directory.write, "committed", b"different")
            # The old link/unlink intermediate state is still rejected as an
            # arbitrary alias, never accepted by weakening the no-hardlink rule.
            os.link(root / "committed", root / ".tmp-foreign-alias")
            self.error(400, directory.write, "committed", b"durable bytes")

    def test_client_request_replay_is_atomic_and_survives_restart(self):
        request = {"transcript": "Make a counter", "draft": "", "source": "on_console", "client_request_id": "p" + "1" * 32}
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
            responses = list(pool.map(lambda _: self.store.submit(request), range(16)))
        self.assertEqual({reply["plan_id"] for reply in responses}, {request["client_request_id"]})
        self.assertEqual(self.store.plan(request["client_request_id"])[0], 202)
        self.assertEqual(len(self.store.outbox()["events"]), 1)
        self.store.limits = (1, 256, 256 * 1024**2, 8 * 1024**3)
        self.assertEqual(self.store.submit(request), responses[0])  # replay survives quota
        for changed in ({"transcript": "Different"}, {"draft": "Changed draft"},
                        {"parent_plan_id": "p" + "2" * 32, "parent_revision": "a" * 64}):
            self.error(409, self.store.submit, {**request, **changed})
        self.store.close()
        self.store = Store(self.base / "data")
        self.assertEqual(self.store.submit(request), responses[0])
        self.store.ingest_plan({"schema": 1, "type": "plan", "request_id": request["client_request_id"], "text": "Build a counter."})
        self.assertEqual(self.store.plan(request["client_request_id"])[0], 200)
        self.assertEqual(self.store.submit(request), responses[0])
        self.error(400, self.store.submit, {**request, "client_request_id": "not-an-id"})

    def test_ledger_survives_database_loss_and_reconciles_legacy_jobs(self):
        job, revision = self.approved()
        original = self.store.read_title_ledger()[0]
        self.assertEqual(original["job_id"], job)
        # A legacy DB without a ledger bootstraps reservations from all jobs.
        (self.store.path / "title-ledger.json").unlink()
        self.store.close()
        self.store = Store(self.base / "data")
        self.assertEqual(self.store.read_title_ledger(), [original])
        # Losing just SQLite cannot recycle title identifiers already reserved.
        self.store.close()
        for suffix in ("", "-wal", "-shm"):
            (self.base / "data" / ("relay.sqlite3" + suffix)).unlink(missing_ok=True)
        self.store = Store(self.base / "data")
        new_job, _ = self.approved()
        entries = self.store.read_title_ledger()
        self.assertEqual(len(entries), 2)
        self.assertIn(original, entries)
        new = next(entry for entry in entries if entry["job_id"] == new_job)
        self.assertNotEqual(original["identity"]["title_id"], new["identity"]["title_id"])

    def test_ledger_reservation_precedes_sqlite_commit(self):
        id, revision = self.ready()
        approval = {"revision": revision, "idempotency_key": "crash", "artifact_kind": "pkg", "inventory": inventory()}
        with patch.object(self.store, "event", side_effect=sqlite3.OperationalError("injected DB commit-path failure")):
            with self.assertRaises(sqlite3.OperationalError):
                self.store.approve(id, approval)
        burned = self.store.read_title_ledger()
        self.assertEqual(len(burned), 1)
        with self.store.db() as db:
            self.assertEqual(db.execute("SELECT count(*) FROM jobs").fetchone()[0], 0)
        job = self.store.approve(id, approval)["job_id"]
        entries = self.store.read_title_ledger()
        self.assertEqual(len(entries), 2)
        self.assertNotEqual(entries[0]["identity"]["title_id"], entries[1]["identity"]["title_id"])
        self.assertEqual(entries[1]["job_id"], job)

    def test_ledger_corruption_and_conflict_fail_closed(self):
        self.approved()
        path = self.store.path / "title-ledger.json"
        original = path.read_bytes()
        path.write_bytes(original.replace(b"Muse App", b"MUSE APP"))
        with self.assertRaises(RelayError):
            self.store.reserve_identity("j" + "4" * 32, inventory())
        with self.assertRaises(RelayError):
            Store(self.store.path)
        self.assertEqual(path.read_bytes(), original.replace(b"Muse App", b"MUSE APP"))
        path.write_bytes(original)
        with self.store.db(True) as db:
            identity = json.loads(db.execute("SELECT identity_json FROM jobs").fetchone()[0])
            identity["name"] = "Conflicting title"
            db.execute("UPDATE jobs SET identity_json=?", (canonical(identity).decode(),))
        with self.assertRaises(RelayError):
            Store(self.store.path)
        self.assertEqual(path.read_bytes(), original)

    def test_concurrent_store_startup_and_ledger_allocations(self):
        path = self.base / "shared-store"
        def initialize_and_reserve(number):
            other = Store(path)
            try:
                return other.reserve_identity("j" + f"{number:032x}", inventory())["title_id"]
            finally:
                other.close()
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
            titles = list(pool.map(initialize_and_reserve, range(16)))
        self.assertEqual(len(set(titles)), 16)
        other = Store(path)
        try:
            self.assertEqual(len(other.read_title_ledger()), 16)
            self.assertNotIn("PPSA99576", titles)
        finally:
            other.close()

    def test_ledger_atomic_write_failure_cannot_commit_job(self):
        id, revision = self.ready()
        approval = {"revision": revision, "idempotency_key": "ledger-write", "artifact_kind": "pkg", "inventory": inventory()}
        before = (self.store.path / "title-ledger.json").read_bytes()
        with patch("relay.store.os.replace", side_effect=OSError("injected ledger rename failure")):
            with self.assertRaises(OSError):
                self.store.approve(id, approval)
        self.assertEqual((self.store.path / "title-ledger.json").read_bytes(), before)
        with self.store.db() as db:
            self.assertEqual(db.execute("SELECT count(*) FROM jobs").fetchone()[0], 0)
        self.assertEqual(len(self.store.outbox()["events"]), 1)

    def test_legacy_request_migration_and_replay(self):
        id = self.pending()
        with self.store.db(True) as db:
            db.execute("ALTER TABLE requests DROP COLUMN request_sha")
        self.store.close()
        self.store = Store(self.base / "data")
        response = self.store.submit({"transcript": "Make a counter", "draft": "", "source": "on_console", "client_request_id": id})
        self.assertEqual(response["plan_id"], id)
        self.assertEqual(len(self.store.outbox()["events"]), 1)

    def test_old_and_recreated_database_instances_share_ledger_after_loss(self):
        self.approved()
        original = self.store.read_title_ledger()[0]
        for suffix in ("", "-wal", "-shm"):
            (self.store.path / ("relay.sqlite3" + suffix)).unlink(missing_ok=True)
        barrier = threading.Barrier(2)
        def old_instance():
            barrier.wait(timeout=5)
            return self.store.reserve_identity("j" + "e" * 32, inventory())
        def recreated_instance():
            barrier.wait(timeout=5)
            other = Store(self.store.path)
            try:
                return other.reserve_identity("j" + "f" * 32, inventory())
            finally:
                other.close()
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            futures = [pool.submit(old_instance), pool.submit(recreated_instance)]
            identities = [future.result(timeout=20) for future in futures]
        self.assertEqual(len({original["identity"]["title_id"], *(identity["title_id"] for identity in identities)}), 3)
        self.assertEqual(len(self.store.read_title_ledger()), 3)

    def test_inventory_exclusions_and_private_audit(self):
        id, revision = self.ready()
        snapshot = inventory("PPSA99003", "PPSA99001", "PPSA99000", "PPSA99576", "PPSA99002", "LAPY00001", "FAKE00001", "CUSA00001")
        body = {"revision": revision, "idempotency_key": "inventory", "artifact_kind": "pkg", "inventory": snapshot}
        job = self.store.approve(id, body)["job_id"]
        with self.store.db() as db:
            row = self.store.row(db, "jobs", job)
        self.assertEqual(json.loads(row["identity_json"])["title_id"], "PPSA99004")
        self.assertEqual(json.loads(row["inventory_json"])["title_ids"], sorted(snapshot["title_ids"]))
        event = self.store.outbox()["events"][-1]["payload"]
        self.assertNotIn("inventory", event)
        self.assertNotIn("title_ids", event)
        # Lost approval response can recover without new inventory or allocation.
        del body["inventory"]
        self.assertEqual(self.store.approve(id, body)["job_id"], job)
        self.assertEqual(len(self.store.read_title_ledger()), 1)

    def test_inventory_missing_or_invalid_cannot_allocate(self):
        id, revision = self.ready()
        body = {"revision": revision, "idempotency_key": "guard", "artifact_kind": "pkg"}
        self.error(428, self.store.approve, id, body)
        for bad in (None, [], {}, {**inventory(), "source": "provider"},
                    {**inventory(), "api_version": True}, {**inventory(), "api_version": 1.0},
                    {**inventory(), "api_version": 2}, {**inventory(), "title_ids": "PPSA99001"},
                    {**inventory(), "complete": True}, inventory("PPSA99001", "PPSA99001"),
                    inventory("PPSA99001\x00"), inventory("../PPSA99001"), inventory("PPSA999999"),
                    inventory(1), inventory(None), inventory(*[f"CUSA{i:05d}" for i in range(4097)])):
            with self.subTest(bad_type=type(bad).__name__):
                self.error(400, self.store.approve, id, {**body, "inventory": bad})
        self.assertEqual(self.store.read_title_ledger(), [])
        with self.store.db() as db:
            self.assertEqual(db.execute("SELECT count(*) FROM jobs").fetchone()[0], 0)
        # No guard failure consumed the idempotency key or changed the plan.
        self.assertIn("job_id", self.store.approve(id, {**body, "inventory": inventory()}))

    def test_inventory_reserves_both_fixed_titles_and_exhaustion(self):
        # Force the allocator to reach MuseStation's slot, without letting it use it.
        snapshot = inventory(*[f"PPSA99{i:03d}" for i in range(1, 576)])
        result = self.store.reserve_identity("j" + "a" * 32, snapshot)
        self.assertEqual(result["title_id"], "PPSA99577")
        self.error(507, self.store.reserve_identity, "j" + "b" * 32,
                   inventory(*[f"PPSA99{i:03d}" for i in range(1000)]))
        self.assertEqual(len(self.store.read_title_ledger()), 1)

    def test_inventory_handles_loss_of_both_database_and_ledger(self):
        # A recreated relay has no history but must still skip the console report.
        self.approved()
        old_title = self.store.read_title_ledger()[0]["identity"]["title_id"]
        self.store.close()
        for name in ("relay.sqlite3", "relay.sqlite3-wal", "relay.sqlite3-shm", "title-ledger.json"):
            (self.base / "data" / name).unlink(missing_ok=True)
        self.store = Store(self.base / "data")
        id, revision = self.ready()
        job = self.store.approve(id, {"revision": revision, "idempotency_key": "recreated", "artifact_kind": "pkg",
                                     "inventory": inventory(old_title, "PPSA99002")})["job_id"]
        self.assertEqual(self.store.read_title_ledger()[0]["job_id"], job)
        self.assertEqual(self.store.read_title_ledger()[0]["identity"]["title_id"], "PPSA99003")

    def test_legacy_reserved_title_and_inventory_column_migrate(self):
        self.approved()
        # Preserve previously-issued identities; adding a reserved ID is not a renumbering migration.
        entry = self.store.read_title_ledger()[0]
        entry["identity"]["title_id"] = "PPSA99000"
        entry["identity"]["content_id"] = "IV0000-PPSA99000_00-MUSE0123456789AB"
        with self.store.ledger_lock():
            self.store.write_title_ledger([entry])
        with self.store.db(True) as db:
            db.execute("UPDATE jobs SET identity_json=?", (canonical(entry["identity"]).decode(),))
            db.execute("ALTER TABLE jobs DROP COLUMN inventory_json")
        self.store.close()
        self.store = Store(self.base / "data")
        self.approved()
        self.assertEqual([e["identity"]["title_id"] for e in self.store.read_title_ledger()], ["PPSA99000", "PPSA99001"])


class HTTPCase(StoreCase):
    def test_http_inventory_required_bounded_and_separate_from_provider(self):
        id, revision = self.ready()
        body = {"revision": revision, "idempotency_key": "http-inventory", "artifact_kind": "pkg"}
        path = "/plan/" + id + "/approve"
        self.assertEqual(self.call(self.console, "POST", path, body)[0], 428)
        body["inventory"] = inventory(*[f"CUSA{i:05d}" for i in range(4096)])
        self.assertGreater(len(canonical(body)), 4096)
        self.assertLess(len(canonical(body)), 64 * 1024)
        self.assertEqual(self.call(self.provider, "POST", path, body, auth=True)[0], 404)
        self.assertEqual(self.call(self.console, "POST", path, body, auth=False)[0], 401)
        self.assertEqual(self.call(self.console, "POST", path, body)[0], 200)
        self.assertEqual(self.call(self.console, "POST", path, raw=b" " * (64 * 1024 + 1),
                                   headers={"Content-Type": "application/json"})[0], 413)

    def setUp(self):
        super().setUp()
        self.token = b"a" * 64
        self.console_token = b"b" * 64
        app = Application(self.store, self.exchange, self.token, console_token=self.console_token)
        self.console = RelayHTTPServer(("127.0.0.1", 0), app)
        self.provider = RelayHTTPServer(("127.0.0.1", 0), app, True)
        self.threads = []
        for server in (self.console, self.provider):
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            self.threads.append(thread)

    def tearDown(self):
        for server in (self.console, self.provider):
            server.shutdown()
            server.server_close()
        for thread in self.threads:
            thread.join()
        super().tearDown()

    def call(self, server, method, path, value=None, auth=None, raw=None, headers=None):
        connection = http.client.HTTPConnection(*server.server_address, timeout=5)
        headers = dict(headers or {})
        if auth or (auth is None and server is self.console):
            headers.setdefault("Authorization", "Bearer " + (self.token if server is self.provider else self.console_token).decode())
        if value is not None:
            raw = canonical(value)
            headers["Content-Type"] = "application/json"
        connection.request(method, path, raw, headers)
        response = connection.getresponse()
        result = response.read()
        status = response.status
        response_headers = dict(response.getheaders())
        connection.close()
        return status, result, response_headers

    def test_provider_auth_and_separate_authority(self):
        status, raw, _ = self.call(self.console, "POST", "/voice", {"transcript": "counter", "draft": "", "source": "on_console"})
        self.assertEqual(status, 200)
        id = json.loads(raw)["plan_id"]
        self.assertEqual(self.call(self.console, "GET", "/plan/" + id)[0], 202)
        value = {"schema": 1, "type": "plan", "request_id": id, "text": "Build a counter."}
        self.assertEqual(self.call(self.provider, "POST", "/provider/plans", value)[0], 401)
        self.assertEqual(self.call(self.provider, "POST", "/provider/plans", value, auth=True)[0], 200)
        status, raw, _ = self.call(self.console, "GET", "/plan/" + id)
        self.assertEqual(status, 200)
        approval = {"revision": json.loads(raw)["revision"], "idempotency_key": "http", "artifact_kind": "pkg", "inventory": inventory()}
        self.assertEqual(self.call(self.provider, "POST", "/plan/" + id + "/approve", approval, auth=True)[0], 404)
        self.assertEqual(self.call(self.console, "POST", "/provider/plans", value)[0], 404)
        self.assertEqual(self.call(self.console, "POST", "/plan/" + id + "/approve", approval)[0], 200)
        status, raw, _ = self.call(self.provider, "GET", "/provider/outbox?after=0", auth=True)
        self.assertEqual(status, 200)
        self.assertEqual([event["type"] for event in json.loads(raw)["events"]], ["request", "approved_job"])
        self.assertEqual(self.call(self.provider, "GET", "/provider/outbox?after=-1", auth=True)[0], 400)
        self.assertEqual(self.call(self.console, "POST", "/voice", raw=b"RIFFaudio", headers={"Content-Type": "audio/wav"})[0], 415)
        status, raw, _ = self.call(self.console, "GET", "/health")
        self.assertEqual(status, 200)
        self.assertFalse(json.loads(raw)["muse_connector_verified"])

    def test_exact_content_length_artifact_and_logs(self):
        job, rev = self.approved()
        self.store.ingest_source(self.source(job, rev))
        class BuilderFixture:
            def build(inner, job_dir, source, identity, output_dir):
                return fixture_package(output_dir / "title.pkg", identity)
        run_one(self.store, BuilderFixture())
        status, raw, headers = self.call(self.console, "GET", f"/build/{job}/artifact")
        manifest = self.store.status(job)
        self.assertEqual(status, 200)
        self.assertEqual(int(headers["Content-Length"]), manifest["size"])
        self.assertNotIn("Transfer-Encoding", headers)
        self.assertEqual(hashlib.sha256(raw).hexdigest(), manifest["sha256"])
        log = b'{"ts":123,"level":"info","event":"boot","detail":"fixture"}\n'
        status, raw, _ = self.call(self.console, "POST", "/logs", raw=log, headers={"Content-Type": "application/x-ndjson"})
        self.assertEqual(status, 200)
        id = json.loads(raw)["log_id"]
        self.assertEqual(self.call(self.provider, "GET", "/provider/logs/" + id)[0], 401)
        status, raw, _ = self.call(self.provider, "GET", "/provider/logs/" + id, auth=True)
        self.assertEqual(status, 200)
        self.assertEqual(raw, log)

    def test_private_token_permissions(self):
        path = self.base / "token"
        path.write_bytes(self.token + b"\n")
        path.chmod(0o600)
        self.assertEqual(read_token(path), self.token)
        path.chmod(0o644)
        with self.assertRaises(ValueError):
            read_token(path)

    def test_console_credentials_guard_all_private_routes_before_body(self):
        id, revision = self.ready()
        approval = {"revision": revision, "idempotency_key": "unauthorized", "artifact_kind": "pkg", "inventory": inventory()}
        routes = [("POST", "/voice"), ("GET", "/plan/" + id),
                  ("POST", "/plan/" + id + "/approve"),
                  ("GET", "/build/j" + "1" * 32 + "/status"),
                  ("GET", "/build/j" + "1" * 32 + "/artifact"), ("POST", "/logs")]
        for method, path in routes:
            for header in ({}, {"Authorization": "Bearer " + self.token.decode()},
                           {"Authorization": "Bearer " + "c" * 64}):
                status, raw, _ = self.call(self.console, method, path, auth=False, headers=header)
                self.assertEqual(status, 401, (method, path, header))
        self.assertEqual(self.call(self.console, "POST", "/plan/" + id + "/approve", approval, auth=False)[0], 401)
        with self.store.db() as db:
            self.assertEqual(db.execute("SELECT count(*) FROM jobs").fetchone()[0], 0)
        # An advertised body is not read until credentials pass: no payload is sent.
        with socket.create_connection(self.console.server_address, timeout=2) as connection:
            connection.sendall(b"POST /voice HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/json\r\nContent-Length: 50000\r\n\r\n")
            self.assertIn(b" 401 ", connection.recv(4096))
        for path in ("/", "/health"):
            status, raw, _ = self.call(self.console, "GET", path, auth=False)
            self.assertEqual(status, 200)
            self.assertNotIn(self.console_token, raw)
            self.assertNotIn(self.token, raw)
        self.assertEqual(self.call(self.provider, "GET", "/provider/outbox", auth=False,
                                  headers={"Authorization": "Bearer " + self.console_token.decode()})[0], 401)

    def test_console_token_required_separate_and_exact_format(self):
        with self.assertRaises(TypeError):
            Application(self.store, self.exchange)
        for invalid in (None, b"", b"z" * 64, b"A" * 64, b"1" * 32):
            with self.assertRaises(ValueError):
                Application(self.store, self.exchange, console_token=invalid)
        with self.assertRaises(ValueError):
            Application(self.store, self.exchange, self.console_token, console_token=self.console_token)
        path = self.base / "console-token"
        path.write_bytes(self.console_token + b"\n")
        path.chmod(0o600)
        self.assertEqual(read_console_token(path), self.console_token)
        path.write_bytes(b"g" * 64)
        with self.assertRaises(ValueError):
            read_console_token(path)
        path.chmod(0o644)
        with self.assertRaises(ValueError):
            read_console_token(path)

    def test_runtime_refuses_missing_console_credential(self):
        directory = self.base / "must-not-start"
        with patch.dict(os.environ, {"MUSE_RELAY_DATA_DIR": str(directory)}, clear=True):
            with self.assertRaisesRegex(ValueError, "MUSE_CONSOLE_TOKEN_FILE is required"):
                server_main()
        self.assertFalse(directory.exists())

    def test_http_voice_idempotency_and_known_id_recovery(self):
        value = {"transcript": "Counter", "draft": "", "source": "on_console", "client_request_id": "p" + "d" * 32}
        first = self.call(self.console, "POST", "/voice", value)
        second = self.call(self.console, "POST", "/voice", value)
        self.assertEqual(first[0], 200)
        self.assertEqual(first[1], second[1])
        status, raw, _ = self.call(self.console, "GET", "/plan/" + value["client_request_id"])
        self.assertEqual(status, 202)
        self.assertEqual(json.loads(raw)["id"], value["client_request_id"])
        self.assertEqual(self.call(self.console, "POST", "/voice", {**value, "transcript": "Different"})[0], 409)
        self.assertEqual(len(self.store.outbox()["events"]), 1)


if __name__ == "__main__":
    unittest.main(verbosity=2)

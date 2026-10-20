import asyncio
import importlib.util
import io
import json
import logging
import os
from pathlib import Path
import stat

import pytest
from musegadget import config
from musegadget.identity import Identity
from musegadget.link_client import Outcome
from musestation_companion.__main__ import SafeLogHandler, main, status
from musestation_companion.service import COMMANDS, StationExecutor, StationService
from musestation_companion.storage import private_state, state_lock

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("upstream_link_fixture", ROOT / "vendor/muse-gadget-sdk/linux/tests/test_link_client.py")
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)


@pytest.fixture(autouse=True)
def isolate_cli_process_globals():
    # main() is normally a process entrypoint. Restore its logging, environment
    # and umask changes when invoking it inside pytest's own process.
    logger = logging.getLogger()
    handlers, level = logger.handlers[:], logger.level
    old_state = os.environ.get(config.STATE_DIR_ENV)
    old_umask = os.umask(0o077)
    os.umask(old_umask)
    yield
    logger.handlers = handlers
    logger.setLevel(level)
    os.umask(old_umask)
    if old_state is None:
        os.environ.pop(config.STATE_DIR_ENV, None)
    else:
        os.environ[config.STATE_DIR_ENV] = old_state


def test_real_noise_session_registers_only_health_and_rejects_other_commands(monkeypatch):
    monkeypatch.setattr("musegadget.executor.Executor.run", lambda *a: pytest.fail("stock executor reached"))

    async def scenario():
        service = StationService(identity=Identity("02:00:00:ab:cd:ef"), executor=StationExecutor())
        to_device, to_vm = asyncio.Queue(), asyncio.Queue()
        device_ws, vm_ws = fixture.Pipe(to_device, to_vm), fixture.Pipe(to_vm, to_device)
        connects = []

        async def connect(url, headers):
            connects.append((url, headers))
            return device_ws

        session = service.make_session({"vm_id": "test-vm", "vm_auth_token": "offline-only-token"}, {}, connect=connect)
        service.make_session = lambda *a: session
        task = asyncio.create_task(service._session({"vm_id": "test-vm"}, {}))
        vm = fixture.FakeVm(vm_ws)
        await vm.handshake()
        await vm.accept_control_stream()
        register = await vm.next_message()
        assert register["method"] == "link.register"
        assert set(register["params"]["commands_v2"]) == {"station.health"}
        assert register["params"]["device_family"] == "homehub"
        assert register["params"]["platform"] == "linux"
        await vm.send_message({"type": "res", "id": register["id"], "ok": True})
        for i, command in enumerate(["station.health", "system.run", "file.read", "file.write", "device.ota", "station.install"]):
            await vm.send_message({"method": "link.invoke", "id": str(i), "command": command, "params": {}})
            result = await vm.next_message()
            assert result["method"] == "link.result" and result["id"] == str(i)
            assert result["ok"] is (command == "station.health")
            if result["ok"]:
                assert result["payload"]["ps5_connected"] is False
                assert result["payload"]["build_supported"] is False
        await vm.send_message({"type": "evt", "event": "link.unpaired"})
        outcome, elapsed = await asyncio.wait_for(task, 2)
        assert outcome is Outcome.UNPAIRED and elapsed >= 0
        assert service._current is None
        assert len(connects) == 1
        with pytest.raises(RuntimeError, match="local chat socket"):
            await service.serve_local(Path("/tmp/not-opened"))

    asyncio.run(asyncio.wait_for(scenario(), 10))


@pytest.mark.parametrize("command", ["system.run", "file.read", "file.write", "station.health\n", "", None])
def test_direct_executor_allowlist(command):
    result = StationExecutor().run(command, {"secret": "do not echo this"})
    assert result == {"ok": False, "error": "command is not allowed"}


@pytest.mark.parametrize("params", [{"extra": True}, [], None, "text"])
def test_health_rejects_parameters(params):
    assert StationExecutor().run("station.health", params)["ok"] is False


def test_private_storage_and_single_writer(tmp_path):
    path = tmp_path / "private"
    private_state(path)
    assert stat.S_IMODE(path.stat().st_mode) == 0o700
    with state_lock(path):
        with pytest.raises(ValueError, match="another companion"):
            with state_lock(path):
                pass
    config.save_json("pairing.json", {"access_token": "offline-access", "refresh_token": "offline-refresh"}, path)
    assert stat.S_IMODE((path / "pairing.json").stat().st_mode) == 0o600
    report = status(path)
    assert report["pairing_stored"] and not report["live_connection_checked"]
    assert "offline-" not in json.dumps(report)
    os.chmod(path / "pairing.json", 0o644)
    with pytest.raises(ValueError, match="private owned"):
        private_state(path)


def test_symlink_state_and_files_rejected(tmp_path):
    target = tmp_path / "target"
    private_state(target)
    link = tmp_path / "state"
    link.symlink_to(target)
    with pytest.raises(ValueError):
        private_state(link)
    (target / "sdk_token").symlink_to(tmp_path / "missing")
    with pytest.raises(ValueError):
        private_state(target)


def test_readonly_status_does_not_create_identity(tmp_path):
    path = tmp_path / "absent"
    assert status(path)["pairing_stored"] is False
    assert not path.exists()


@pytest.mark.parametrize("name", ["identity.json.tmp", "pairing.json.tmp"])
@pytest.mark.parametrize("kind", ["symlink", "shared", "hardlink"])
def test_sdk_temporary_files_are_private_regular_single_links(tmp_path, name, kind):
    directory = private_state(tmp_path / "state")
    path = directory / name
    if kind == "symlink":
        path.symlink_to(tmp_path / "outside")
    else:
        path.write_text("old incomplete state")
        path.chmod(0o600 if kind == "hardlink" else 0o644)
        if kind == "hardlink":
            os.link(path, tmp_path / "second-link")
    with pytest.raises(ValueError, match="private owned"):
        private_state(directory)


def test_logs_drop_tokens_server_text_and_tracebacks():
    stream = io.StringIO()
    handler = SafeLogHandler(stream)
    for message, args in [("server error token=%s", ("secret-value",)), ("mgst_verysecret", ()), ("registered with the Muse", ())]:
        record = logging.LogRecord("musegadget", logging.INFO, "", 0, message, args, None)
        record.exc_text = "secret traceback"
        handler.emit(record)
    output = stream.getvalue()
    assert "secret" not in output and "token" not in output
    assert [json.loads(x)["event"] for x in output.splitlines()] == ["sdk_event", "sdk_event", "registered"]
    stream.close()
    handler.emit(logging.LogRecord("musegadget", logging.ERROR, "", 0, "never-print-this", (), None))


def test_missing_pairing_fails_without_network(tmp_path, monkeypatch, capsys):
    monkeypatch.setattr("musegadget.muse_api.fetch_vms_with_status", lambda *a: pytest.fail("network attempted"))
    assert main(["--state-dir", str(tmp_path / "private"), "run"]) == 2
    assert "operation_failed" in capsys.readouterr().err


def test_pair_requires_token_before_ble_or_network(tmp_path, monkeypatch, capsys):
    monkeypatch.delenv(config.SDK_TOKEN_ENV, raising=False)
    monkeypatch.setattr("musegadget.muse_api.fetch_vms_with_status", lambda *a: pytest.fail("network attempted"))
    assert main(["--state-dir", str(tmp_path / "private"), "pair"]) == 2
    assert "operation_failed" in capsys.readouterr().err


def test_sdk_pin_manifest_exact():
    import runpy
    runpy.run_path(str(ROOT / "verify_vendor.py"))


def test_unexpected_sdk_error_cannot_print_credentials(tmp_path, monkeypatch, capsys):
    def fail(*args, **kwargs):
        raise RuntimeError("unexpected access_token=never-print-this")
    monkeypatch.setattr("musestation_companion.__main__.private_state", fail)
    assert main(["--state-dir", str(tmp_path / "private"), "status"]) == 2
    output = capsys.readouterr().err
    assert "never-print-this" not in output and "RuntimeError" in output

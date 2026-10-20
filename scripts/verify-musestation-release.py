#!/usr/bin/env python3
"""Independently check release identity, package hash, and locked model/helper bytes."""
import argparse
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    parser.add_argument("--version", default="01.002.000")
    args = parser.parse_args()
    assert re.fullmatch(r"[0-9]{2}\.[0-9]{3}\.[0-9]{3}", args.version)
    package = args.directory / "IV0000-PPSA99576_00-MUSEGADGET000001.pkg"
    report = json.loads(package.with_suffix(".pkg.report.json").read_text())
    assert report["Title"]["Name"] == "MuseStation"
    assert report["Title"]["Version"] == args.version
    assert report["Title"]["TitleId"] == "PPSA99576"
    assert report["Title"]["ContentId"] == "IV0000-PPSA99576_00-MUSEGADGET000001"
    assert report["HardwareVerified"] is False
    assert report["StructuralValidation"]["Accepted"]
    assert all(c["Status"] == "Pass" for c in report["StructuralValidation"]["Checks"])
    assert report["RoundTripFileCount"] == len(report["Files"])
    assert package.stat().st_size == report["Bytes"]
    assert digest(package) == report["Sha256"]
    files = {f["path"]: f for f in report["Files"]}
    assert len(files) == len(report["Files"])
    stage = ROOT / "build/native/title/PPSA99576"
    assert set(files) == {p.relative_to(stage).as_posix() for p in stage.rglob("*") if p.is_file()}
    transformations = []
    for path, entry in files.items():
        source = stage / path
        assert digest(source) == entry["sourceSha256"], path
        expected = entry["sourceSha256"]
        if path == "sce_sys/param.json":
            content = source.read_text()
            for key in ("sdkVersion", "requiredSystemSoftwareVersion"):
                content = re.sub(r'("' + key + r'"\s*:\s*")0x0+(")',
                                 r'\g<1>0x0200000000000000\2', content, count=1)
            encoded = content.encode()
            expected = hashlib.sha256(encoded).hexdigest()
            assert entry["bytes"] == len(encoded)
            if expected != entry["sourceSha256"]:
                assert entry["transformation"] == "upstream zero SDK/firmware version promotion to 2.00"
                transformations.append({"path": path, "transformation": entry["transformation"]})
        else:
            assert entry["transformation"] == "none", path
            assert entry["bytes"] == source.stat().st_size, path
        assert entry["sha256"] == expected, path
    models = []
    for lock in ("lock.json", "neural-lock.json"):
        models += json.loads((ROOT / "inference" / lock).read_text())["models"]
    expected_weights = {"assets/inference/" + m["name"] for m in models}
    actual_weights = {p for p in files if p.startswith("assets/inference/") and p.endswith((".gguf", ".bin"))}
    assert actual_weights == expected_weights
    for model in models:
        f = files["assets/inference/" + model["name"]]
        assert f["sha256"] == model["sha256"] and f["bytes"] == model["bytes"]
    helper = ROOT / "build/native/title/PPSA99576/assets/install/muse-install-helper.payload"
    assert files["assets/install/muse-install-helper.payload"]["sha256"] == digest(helper)
    assert files["licenses/LUA-MIT.txt"]["sha256"] == digest(ROOT / "native/lua/LICENSE.txt")
    result = {
        "title": "MuseStation", "version": args.version, "package": package.name,
        "bytes": report["Bytes"], "sha256": report["Sha256"],
        "verified_files": len(files), "structural_checks": len(report["StructuralValidation"]["Checks"]),
        "locked_models": [m["name"] for m in models], "model_bytes": sum(m["bytes"] for m in models),
        "verified_metadata_transformations": transformations,
        "eboot": files["eboot.bin"], "helper": files["assets/install/muse-install-helper.payload"],
        "hardware_verified": False,
    }
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Write a ProsperoStore custom catalog (store API v1, schema 3) listing one MuseStation ZIP.

The files go under <site>/api/v1/ of the MuseStation website, which GitHub Pages serves
over HTTPS at musestation.interestingsoup.com. In ProsperoStore, add that full API address
under Settings > Catalogs (the InterestingSoup/musestation shorthand resolves to github.io,
which redirects to plain HTTP). The catalog is unsigned, so the store labels it "Signatures
not checked"; artifact SHA-256, ZIP validation and HTTPS checks still apply on the console.
"""
import argparse
import hashlib
import json
import re
from datetime import datetime, timezone
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
TITLE = "PPSA99576"
DESCRIPTION = "Describe a small app, approve the plan, and MuseStation compiles and installs it on your PS5."


def minified(value):
    return (json.dumps(value, ensure_ascii=False, separators=(",", ":"), sort_keys=False) + "\n").encode()


def put(api, files, relative, value):
    data = minified(value)
    target = api / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)
    files[relative] = hashlib.sha256(data).hexdigest()


def icons(api, source, base):
    folder = api / "icons"
    folder.mkdir(parents=True, exist_ok=True)
    with Image.open(source) as image:
        image = image.convert("RGBA" if "A" in image.getbands() else "RGB")
        large = image.copy()
        large.thumbnail((512, 512))
        large.save(folder / f"{TITLE}.png", optimize=True)
        small = image.copy()
        small.thumbnail((256, 256))
        small.save(folder / f"{TITLE}-256.png", optimize=True)
    fingerprint = hashlib.sha256((folder / f"{TITLE}.png").read_bytes()).hexdigest()[:16]
    return (f"{base}icons/{TITLE}.png?v={fingerprint}",
            f"{base}icons/{TITLE}-256.png?v={fingerprint}", fingerprint)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("report", type=Path, help="PPSA99576.zip.report.json from package-folder.py")
    parser.add_argument("output", type=Path, nargs="?", default=ROOT / "site", help="Pages site root")
    parser.add_argument("--source-repo", default="https://github.com/InterestingSoup/musestation")
    parser.add_argument("--api-url", default="https://musestation.interestingsoup.com/api/v1/")
    parser.add_argument("--tag", required=True)
    parser.add_argument("--released", required=True, help="Release publication time, ISO 8601 UTC")
    parser.add_argument("--commit", required=True, help="Source commit the release tag names")
    parser.add_argument("--sequence", type=int, required=True, help="Grows with every catalog change")
    parser.add_argument("--notes", type=Path, help="Plain-text release notes")
    parser.add_argument("--prerelease", action="store_true")
    parser.add_argument("--icon", type=Path, default=ROOT / "build/native/title" / TITLE / "sce_sys/icon0.png")
    args = parser.parse_args()

    report = json.loads(args.report.read_text())
    assert report["titleId"] == TITLE and report["format"] == "zip"
    assert re.fullmatch(r"[0-9]{2}\.[0-9]{3}\.[0-9]{3}", report["version"])
    assert re.fullmatch(r"[0-9a-f]{64}", report["sha256"]) and report["bytes"] <= 2 << 30
    assert re.fullmatch(r"[0-9a-f]{40}", args.commit)
    assert re.fullmatch(r"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z", args.released)
    base = args.api_url
    assert base.startswith("https://") and base.endswith("/api/v1/")
    api = args.output / "api/v1"
    api.mkdir(parents=True, exist_ok=True)
    icon, icon_small, icon_hash = icons(api, args.icon, base)
    version = args.tag[1:] if args.tag.startswith("v") else args.tag
    notes = args.notes.read_text().strip()[:4000] if args.notes else None
    now = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    release_url = f"{args.source_repo}/releases/tag/{args.tag}"
    common = {
        "id": TITLE, "titleid": TITLE, "status": "available", "name": report["title"],
        "kind": "app", "category": "ai", "author": "InterestingSoup", "version": version,
        "content_version": report["version"], "format": "zip", "size": report["bytes"],
        "released": args.released, "updated": now, "added": now,
        "icon_small": icon_small, "icon_hash": icon_hash,
    }
    detail = {
        "schema": 3, **common,
        "description": DESCRIPTION, "license": "GPL-3.0", "source_repo": args.source_repo,
        "artifact_url": f"{args.source_repo}/releases/download/{args.tag}/{report['archive']}",
        "sha256": report["sha256"], "icon_url": None, "artifact_name": report["archive"],
        "tag": args.tag, "prerelease": args.prerelease, "release_url": release_url,
        "release_notes": notes, "release_notes_truncated": False if notes else None,
        # One helper payload goes to the local ELF loader to install generated apps.
        "safety": {"sandbox": "leaves", "routes": ["loader"], "helpers": 1, "helpers_unapproved": 1,
                   "network": True, "build": "developer", "build_workflow": None},
        "conflicts_with": [], "page": args.source_repo, "icon": icon,
    }
    entry = {**common, "sandbox": "leaves", "owner": args.source_repo.split("/")[3], "downloads": None,
             "conflicts_with": []}
    files = {}
    put(api, files, f"apps/{TITLE}.json", detail)
    put(api, files, "index.json", {"schema": 3, "generated": now, "commit": args.commit, "count": 1,
                                   "apps": [entry]})
    put(api, files, "versions.json", {"schema": 3, "apps": {TITLE: {"content_version": report["version"],
                                                                    "version": version}}})
    (api / "manifest.json").write_bytes(minified({"commit": args.commit, "files": dict(sorted(files.items())),
                                                  "schema": 3, "sequence": args.sequence}))
    print(f"Catalog written to {api} ({base})")


if __name__ == "__main__":
    main()

# Repository and deployment layout

The canonical repository is
[moiz-qureshi/musestation](https://github.com/moiz-qureshi/musestation).
It contains both the application and the website. Its first two commits contained
only the landing page; the consolidation adds the existing development source
without replacing that history.

## Where each part runs

| Location | Role |
|---|---|
| GitHub repository | Versioned source, Docker definitions, build scripts, dependency locks, tests and documentation |
| Development Mac | Editing and Git operations; it does not need a working Docker daemon when builds run remotely |
| Opti3, or another Linux x86-64 Docker host | SDK/native/package compilation, relay API, exchange and compiler worker |
| PS5 | Installed MuseStation title, local engines and generated apps; hardware validation remains pending |
| GitHub Pages | Static files from `site/` only |

The development checkout and Opti3 deployment retain the folder name `ps5-muse`.
That does not make them separate repositories. New clones normally use
`musestation`. Absolute runtime paths are configured in `.env.relay`, so renaming
an existing deployment directory is unnecessary.

## What is checked in

The root Compose stack includes the toolchain, relay and builder definitions.
`relay/build/` contains source code for the fixed native build recipe and must
remain tracked. `native/`, `src/` and `inference/` contain the console source.
`packaging/` contains the package builder wrapper and its eight upstream patches.
The dependency locks and fetch scripts retrieve large upstream engine source
trees, SDK releases and model weights. The native UI kit used for compilation is
preserved in `vendor/native-ui/` with its source revision, GPL license, font
licenses and third-party notices. Its upstream GitHub URL returned 404 during
publication, so a new checkout uses this snapshot rather than relying on another
download. The snapshot's non-documentation files match the kit on Opti3; generated
upstream documentation screenshots are omitted. Other downloadable dependencies
still require network access. The small, hash-verified Muse companion SDK
subset is included with its Apache license; that prototype remains separate
from the working relay.

Build outputs (`dist/`, `build/`), downloaded dependencies, model weights, logs,
live configuration, credentials, databases and exchange jobs are excluded.
`.env.example` is the configuration template. No public binary release is
implied by publishing source. When distributing a package, supply its matching
checksums, verification report and corresponding-source archive as described in
[the package source instructions](../packaging/SOURCE-BUILD.md).

## Working on the same code

Make changes in a checkout, review the diff, commit, and push to this repository.
It is equally possible to do that on the Mac or on Opti3. There is no requirement
to push Docker's running files or container layers back to GitHub.

Before updating a deployment, check for local source changes and preserve them:

```sh
git status --short
git fetch origin
git diff HEAD..origin/main -- compose.yaml relay scripts native src inference packaging
```

With a clean source checkout, use `git pull --ff-only`. Never use `git clean`,
delete runtime folders, or overwrite `.env.relay` to synchronize a deployment.
Git ignores the private state, but ignoring it is not a backup. Back it up as
described in [relay operations](RELAY-DEPLOY.md#verification-and-service-operation).

For reviewed relay changes, after any needed tests and state backup:

```sh
docker compose --env-file .env.relay --profile relay config --quiet
docker compose --env-file .env.relay --profile relay build relay
docker compose --env-file .env.relay --profile relay up -d relay builder
docker compose --env-file .env.relay --profile relay ps
```

Source changes to the compiler, native kit or packager need their corresponding
image/kit rebuilds and updated immutable IDs in `.env.relay`; rebuilding the
relay image alone does not refresh those inputs. These commands are operator
steps, not a GitHub Actions deployment. The only automatic deployment in this
repository is the website workflow.

The source consolidation itself leaves the running Docker services and their
private state intact. It does not establish a real Muse account connection or
PS5 compatibility. Existing [verification reports](BUILD-REPORT-INVENTORY.md)
describe what was compiled and tested before publication.

## Consolidation checks — 2026-10-19

A source-only export of the staged Git tree was checked in a separate temporary
directory on Opti3, away from the live runtime:

- All 81 relay tests passed in Docker with network access disabled.
- The relay Docker image built successfully from that exported source.
- Compose configuration validated using the existing deployment settings.
- The 47-file companion SDK subset passed its pinned hash verification.
- Native UI bootstrap accepted the included revision-marked source snapshot.
- Published files contained neither live relay key; common credential-pattern
  and private/generated-path checks also passed.
- First-party documentation file links resolved within the published source.

No application logic changed during consolidation, and no new PS5 compilation
or hardware verification is claimed here. The existing relay remained healthy
with a recent worker heartbeat throughout these checks.

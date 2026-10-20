# MuseStation

**Make something worth playing.**

MuseStation is an experimental homebrew creation studio targeting a PS5 Slim on
firmware 12.40. Bring an idea, review the plan, and explicitly approve the build.

This repository contains the **public landing page and project overview**.
The native application, development packages, model files and private server
configuration are not distributed in this website repository.

**Site:** https://musestation.interestingsoup.com/

## Two ways to create

- **Hybrid with Muse:** local transcription and speech, with planning/development
  through Muse and compilation on an owner-hosted Docker relay. The console
  downloads and checks the package before the installer/launch workflow.
- **Local lab:** a bundled small coding model makes bounded Lua apps that run
  inside MuseStation. No cloud connection is required. Standalone offline PS5
  package compilation is not part of this mode.

Both modes require explicit plan approval. Local lab requires a separate Play
action. Keep MuseStation open and the console awake for local generation.

## Project status

Development candidate: **01.005.000**.

| Area | Evidence / status |
|---|---|
| Native application | Compiled on a Linux Docker build host |
| All-in-one package | Full extraction/hash checks completed |
| Relay request-to-download workflow | Host tested with a controlled provider fixture |
| Package installation and app launch | PS5 hardware validation pending |
| GPU execution, mic capture and speech | Integrated; PS5 hardware validation pending |
| Real Muse connector | Integration pending |
| QR/device-code login | Not implemented |
| Other AI provider connectors | Not implemented |
| Background generation in rest mode / another title | Not supported |

The website's interactive examples are handcrafted browser illustrations. They
do not contact an AI service, compile code, connect to a console, or install apps.
No on-console verification is claimed. A compatible exploit, loader, native
installer and ShadowMountPlus environment must be established separately. An
upstream exploit firmware range alone does not establish app compatibility.
Do not update your console to try this project.

## Website development

The site is buildless HTML, CSS and JavaScript. It makes no analytics, account or
AI requests and loads no third-party fonts or scripts.

```sh
python3 -m http.server 4173 --directory site --bind 127.0.0.1
```

Open http://127.0.0.1:4173. No package installation or build step is required.

GitHub Actions deploys only `site/` to GitHub Pages when `main` changes.
See [hosting notes](docs/HOSTING.md) for the custom domain configuration.

An [InterestingSoup](https://interestingsoup.com/) experiment.
Independent homebrew project; not affiliated with Sony Interactive Entertainment.

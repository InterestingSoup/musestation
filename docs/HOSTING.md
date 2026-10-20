# Hosting

Publish the `site/` directory with GitHub Pages. The repository uses a buildless
Actions workflow and relative asset paths, so it also works at the default
project Pages URL.

## Custom domain

Intended hostname: `musestation.interestingsoup.com`.

1. Enable GitHub Pages with GitHub Actions as the source.
2. Set the custom domain in the repository's Pages settings.
3. At the authoritative DNS provider, create:

   | Type | Name | Target | Proxy |
   |---|---|---|---|
   | CNAME | musestation | moiz-qureshi.github.io | DNS only during GitHub certificate setup |

4. Wait for GitHub's DNS check and certificate provisioning, then enable HTTPS
   enforcement.

The CNAME target excludes the repository name. With an Actions publishing
workflow, a checked-in `CNAME` file is not a substitute for the Pages custom-domain
setting. Do not change unrelated DNS records.

Sources: [GitHub custom domains](https://docs.github.com/en/pages/configuring-a-custom-domain-for-your-github-pages-site/managing-a-custom-domain-for-your-github-pages-site),
[GitHub Pages workflows](https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages).

## Search and link previews

Share **https://musestation.interestingsoup.com/** for the project introduction.
The separate `musestation-api` hostname is an authenticated service, not the
public landing page.

The title is **MuseStation — PS5 Homebrew Creation Studio**. Metadata lives in the
initial HTML so crawlers do not need to execute JavaScript:

- A descriptive page title and description, plus the canonical HTTPS URL.
- Open Graph site name, locale, title, description and image metadata.
- An explicit X/Twitter large-image card with its own title, description and alt text.
- A public 1200×630 PNG at `assets/social-ps5-studio-v1.png`.
- WebSite, WebPage and SoftwareSourceCode JSON-LD, linking the public repository
  and license without invented reviews, ratings or a binary download.
- Crawlable `robots.txt`, a sitemap with the page's modification date, and PNG/SVG
  favicons plus an Apple touch icon.

The share image names the product category and keeps experimental status visible.
Its editable source is `site/assets/social-ps5-studio-v1.svg`. The old `social.png`
remains available for previously fetched pages. When replacing share artwork,
use a new versioned filename and update the image URLs in the HTML and JSON-LD.

To render the committed assets after editing the SVGs, run from the repository root:

```sh
site_art_tools="$(mktemp -d)"
npm install --prefix "$site_art_tools" --no-save --package-lock=false sharp@0.35.4
NODE_PATH="$site_art_tools/node_modules" node scripts/render-site-assets.cjs
```

Push changes under `site/` to `main` to trigger the Pages workflow. After deployment,
verify that the public HTML contains the new metadata and that the declared image
URL returns PNG bytes with the declared dimensions. A local render or successful
Git push alone does not confirm that a chat service has fetched the new preview.
Metadata helps describe the page; search engines still choose their displayed
title and snippet.

References: [Open Graph metadata](https://ogp.me/),
[Google title links](https://developers.google.com/search/docs/appearance/title-link),
[Google robots directives](https://developers.google.com/search/docs/crawling-indexing/robots-meta-tag),
and [SoftwareSourceCode schema](https://schema.org/SoftwareSourceCode).

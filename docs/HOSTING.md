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

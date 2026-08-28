<p align="right">
  <a href="CI-build-and-release.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Automated Build, Firmware Release, and Pages Deployment

`.github/workflows/pipeline.yml` is the single validation and publishing workflow. It checks the repository and host behavior, builds the ESP32-C3 firmware, publishes firmware releases, and deploys one unified Web Bluetooth installer to GitHub Pages. The workflow does not rebuild, catalog, commit, or publish repository PAP files.

## Triggers

- **Pull request:** runs repository, host, installer-output, and firmware checks without write or release permission.
- **Push to `main`:** runs the same checks, refreshes the rolling `latest` firmware prerelease, and deploys Pages.
- **Push of a `v*` tag:** runs the same checks and publishes an immutable versioned firmware Release before deploying Pages.
- **Manual dispatch:** runs the publication path for the selected ref.

## Pipeline

1. The static job runs `./tools/validate.sh --static`. The shared gate checks repository rules, host behavior, the `.pap` packer, Web Bluetooth protocol helpers, and the exact installer-only Pages output.
2. The firmware job runs `./tools/validate.sh --firmware` with ESP-IDF 5.5.3 for ESP32-C3, verifies the merged image, and uploads it as an internal workflow artifact.
3. For non-PR events, the release job creates or refreshes `latest`, or creates the version-tag Release. Releases contain only `FoloToy-AI-Passport-full.bin` and `SHA256SUMS`.
4. In parallel, the Pages job uses GitHub's `actions/configure-pages` to read or enable the Pages site, builds the dependency-free artifact directly from `web/installer.html`, `web/installer.mjs`, and `web/passport-install-protocol.mjs`, and uploads it.
5. The deploy job publishes the uploaded artifact through GitHub Pages. Pages configuration and firmware publication are separate jobs, so a Pages setup failure does not block the BIN Release.

All Actions are pinned to full commit SHAs. Validation uses `contents: read`; only the firmware Release job receives `contents: write`; Pages configuration and deployment use the dedicated `pages` and OIDC permissions.

## Pages enablement

An existing Pages site needs no extra credential: the workflow reads its configuration with `GITHUB_TOKEN` and deploys through GitHub Actions. To let a fresh repository or fork enable Pages automatically, add a repository Actions secret named `PAGES_TOKEN` containing a token with Pages write permission. GitHub does not allow the workflow's generated `GITHUB_TOKEN` to create a Pages site. If this secret is intentionally omitted, enable Pages once in repository settings and choose **GitHub Actions** as the build source.

## PAP and Pages boundary

Repository example PAPs are developer-maintained outputs produced explicitly with `tools/pack_pap.py`; CI no longer regenerates or commits `examples/**/dist/*.pap`, and Releases no longer contain PAP files or a package catalog.

The public site is available at `https://rvaim.github.io/ai-passport/` after Pages is enabled. It contains only the shared installer: enter the public pairing code, choose a local `.pap` plug-in or theme, connect the matching Passport, and install. The selected file stays in the browser and is never uploaded. Web Bluetooth requires a secure context and desktop Chrome or Edge.

For board and flashing details, see [the hardware development guide](../hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md).

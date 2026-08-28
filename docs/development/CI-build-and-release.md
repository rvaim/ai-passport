<p align="right">
  <a href="CI-build-and-release.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Automated Build, Firmware Release, and Pages Deployment

`.github/workflows/pipeline.yml` is the single validation and publishing workflow. It checks the repository and host behavior, builds the ESP32-C3 firmware, publishes firmware releases, and deploys one shared Web Bluetooth device tool to GitHub Pages. The workflow does not rebuild, catalog, commit, or publish repository PAP files.

## Triggers

- **Pull request:** runs repository, host, installer-output, and firmware checks without write or release permission.
- **Push to `main`:** runs the same checks, refreshes the rolling `latest` firmware prerelease, and deploys Pages.
- **Push of a `v*` tag:** runs the same checks and publishes an immutable versioned firmware Release before deploying Pages.
- **Manual dispatch:** runs the publication path for the selected ref.

## Pipeline

1. The static job uses ESP-IDF 5.5.3 to materialize the Managed Components pinned by `dependencies.lock` in an isolated build directory, then runs `./tools/validate.sh --static`. The shared gate checks repository rules, host behavior, the `.pap` packer, Web Bluetooth protocol helpers, the 2FA PAP, and the exact Pages output.
2. The firmware job runs `./tools/validate.sh --firmware` with ESP-IDF 5.5.3 for ESP32-C3, verifies the merged image, and uploads it as an internal workflow artifact.
3. For non-PR events, the release job creates or refreshes `latest`, or creates the version-tag Release. Releases contain only `FoloToy-AI-Passport-full.bin` and `SHA256SUMS`.
4. In parallel, the Pages job uses GitHub's `actions/configure-pages` to read the configured Pages site, builds the dependency-free artifact directly from the shared page and its install, Link-frame, and TOTP protocol modules under `web/`, and uploads it.
5. The deploy job publishes the uploaded artifact through GitHub Pages. Pages configuration and firmware publication are separate jobs, so a Pages setup failure does not block the BIN Release.

All Actions are pinned to full commit SHAs. Validation uses `contents: read`; only the firmware Release job receives `contents: write`; Pages configuration and deployment use the dedicated `pages` and OIDC permissions.

## Pages enablement

The `rvaim/ai-passport` Pages site is configured with **GitHub Actions** as its build source. The workflow reads that configuration and deploys with the short-lived `GITHUB_TOKEN`; no Personal Access Token or `PAGES_TOKEN` secret is required. A fresh fork must enable Pages once in repository settings and select **GitHub Actions**, because GitHub does not allow the workflow-generated token to create the Pages site itself.

## PAP and Pages boundary

Repository example PAPs are developer-maintained outputs produced explicitly with `tools/pack_pap.py`; CI no longer regenerates or commits `examples/**/dist/*.pap`, and Releases no longer contain PAP files or a package catalog.

The public site is available at `https://rvaim.github.io/ai-passport/` after Pages is enabled. Its top-level device-code connection is shared by the local `.pap` installer and the 2FA key sender. Files and account data stay between the browser and Passport and are never uploaded; the TOTP secret uses the existing plaintext Link boundary. No PAP file or package catalog is bundled into the Pages artifact. Web Bluetooth requires a secure context and desktop Chrome or Edge.

For board and flashing details, see [the hardware development guide](../hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md).

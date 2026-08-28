<p align="right">
  <a href="CI-build-and-release.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Automated Build, Package, Release, and Pages

`.github/workflows/pipeline.yml` is the single publishing workflow. It checks
pull requests, rebuilds every repository PAP, commits generated `dist/*.pap`
files on `main`, builds the ESP32-C3 firmware, publishes packages and firmware,
and deploys the static GitHub Pages site.

## Triggers

- **Pull request**: validates the current PAP outputs, site catalog, web
  installer, repository rules, and host tests; no write or release permission is
  granted.
- **Push to `main`**: rebuilds PAPs, updates only changed generated files in
  their source directories, publishes the rolling `latest` prerelease, and
  deploys Pages.
- **Push of a `v*` tag**: rebuilds PAPs and publishes an immutable versioned
  Release with the firmware, every plugin/theme package, catalog, and checksums.
- **Manual dispatch**: runs the same publication path for the selected ref.

## Pipeline

1. The package job scans `examples/**/manifest.json`, validates the exact
   current schema, regenerates each package with `tools/pack_pap.py`, rejects
   stale or duplicate outputs, and writes a deterministic catalog. On `main`, a
   separate least-privilege job commits only generated `dist/*.pap` changes with
   `github-actions[bot]` and pushes them back to `main` after static and firmware
   checks pass. The bot token does not recursively start another workflow.
2. The static job restores the generated PAPs into the checkout, runs
   `./tools/validate.sh --static`, builds the site in a temporary directory, and
   verifies package, documentation, and installer links.
3. The firmware job runs `./tools/validate.sh --firmware` with ESP-IDF 5.5.3 for
   ESP32-C3 and uploads the verified merged image.
4. The publish job creates or refreshes `latest` for branch builds, or creates a
   versioned Release for a tag. It uploads `FoloToy-AI-Passport-full.bin`, all
   `.pap` assets, `catalog.json`, and `SHA256SUMS`.
5. The same build copies the generated packages into the Pages artifact. The
   site reads its same-origin `data/catalog.json` and package files, so its
   install links cannot depend on GitHub API directory listings or cross-origin
   redirects.

All Actions are pinned to full commit SHAs. Read-only checks run with
`contents: read`; only the generated-output commit and Release job receive
`contents: write`; Pages deployment uses the dedicated `pages` and OIDC
permissions. In repository settings, set Pages → Build and deployment → Source
to **GitHub Actions** once before the first deployment.

## Generated outputs

The checked-in `examples/**/dist/*.pap` files are release artifacts produced from
their neighboring source directories. Do not edit them by hand. Running
`python3 tools/build_pap_catalog.py --check` verifies that they match the current
Manifest and payload; the workflow uses `--write --prune` before committing
changed outputs.

The public site is available at
`https://rvaim.github.io/ai-passport/` after Pages is enabled. Its plugin/theme
cards use the generated catalog and open the existing Web Bluetooth installer
with the selected package preloaded. Browser-side installation still requires a
secure context and desktop Chrome or Edge.

For board and flashing details, see [the hardware development guide](../hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md).

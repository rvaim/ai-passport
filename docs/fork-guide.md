<p align="right">
  <a href="fork-guide.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Fork Workflow

The upstream repository keeps `main` as the current FoloToy baseline. Fork-specific firmware belongs on `feature/*` branches so a fork can continuously synchronize its `main` without mixing product work into the baseline.

## Repository roles

```text
docs/                  product, contribution, development, and design documents
components/bsp/        stable board APIs and hardware implementation
main/                  Passport system launcher, plug-in manager, settings, and themes
assets/                reusable fonts, images, music, and sound effects
skills/                reusable AI-agent skills
tests/                 host-runnable logic tests
sdkconfig.defaults     reproducible ESP32-C3 defaults
```

The root `README.md` path is intentionally available to a fork owner. Upstream's project overview is `docs/README.md`, which GitHub displays when no root README exists. A fork may add its own root README to explain its product without replacing upstream documentation.

## Fork rules

- Keep fork `main` synchronized manually with `FoloToy/ai-passport:main`, using GitHub's **Sync fork** control or an explicit Git fetch/merge workflow before starting feature work.
- On fork `main`, limit fork-owned content to a root `README.md` pair and `docs/assets/`; develop firmware and other changes on `feature/*` branches and merge by pull request.
- Enable the validation and publishing GitHub Actions manually after forking when the fork needs them. This repository does not include an automatic upstream-sync workflow.

Use `docs/assets/` for architecture notes, product design, and images that supplement a fork's README. Upstream keeps that directory empty except for `.gitkeep`; fork-private content must not be proposed back to upstream.

All fork documentation follows the repository language rule: English at the default `.md` path and Simplified Chinese at `.zh_CN.md`, with reciprocal switches.

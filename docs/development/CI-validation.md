<p align="right">
  <a href="CI-validation.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Pull Request Validation

`.github/workflows/pipeline.yml` runs the validation jobs for pull requests, pushes to `main`, version tags, and manual dispatch. Local development and CI share `tools/validate.sh`; publication happens only after these checks pass.

## Jobs

- **Repository and host checks:** validates English-default bilingual Markdown, relative links, full-SHA Actions, issue forms, the dependency lock, conflict markers, and likely sensitive data; then runs `actionlint` and host tests.
- **ESP-IDF 5.5.3 firmware:** runs `./tools/validate.sh --firmware` for ESP32-C3 in a fresh isolated build/configuration directory, verifies the build and merged `0x0` image contents/offsets, and uploads the verified image for the publication job.

The validation jobs have only `contents: read` and use no repository secrets, so they can validate fork pull requests. Separate publication jobs receive write permissions only when the event is eligible. Every GitHub Action is pinned to a full commit SHA.

## Reproduce locally

```bash
./tools/validate.sh --static
get_idf553
./tools/validate.sh --firmware
```

Reproduce a CI failure with the same mode locally. Do not maintain duplicate validation commands inside the workflow.

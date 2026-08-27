<p align="right">
  <a href="CHANGELOG.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Changelog

## Unreleased

- Added one bounded, cJSON-backed `passport.json` system API shared by every PAP. Plug-ins can decode JSON to Lua values, encode Lua values to compact JSON, preserve nested nulls and empty arrays, and receive recoverable `nil, error` failures; host tests cover round trips, UTF-8, duplicate keys, depth/node/size limits, unsafe numbers, sparse tables, cycles, and unsupported types.
- Removed unreleased manifest and theme compatibility paths: packages now require the exact current schema and API `1`, unknown fields including the unused `permissions` placeholder are rejected, and themes must provide all 18 tokens in canonical `#RRGGBB` form. The device validates complete theme tokens before committing an installation, while one shared parser and host suite keep packer and firmware rules aligned.
- Fixed intermittent command-line installation failures for larger PAPs. The root cause was unacknowledged BLE writes overrunning the device's bounded eight-entry receiver queue; the CLI now waits for receiver readiness, uses acknowledged chunks for backpressure, waits for the final device result, and returns failure instead of optimistically exiting after two seconds. A fake-BLE host test locks the transport behavior.
- Added the installable Agent authorization panel PAP and an editable Web Bluetooth demo. The panel displays one compact JSON request, lets the user choose up to three options, sends the result over Passport Link, and replays duplicate request IDs for delivery retries; the demo connects to an already-open panel, previews the exact 200-byte-bounded payload, sends requests or cancellations, and shows device responses.
- Expanded installable themes from eight palette/spacing fields to eighteen bounded visual tokens, including list surfaces, selection text, borders, radii, and shadow geometry. Shared list components now render those tokens with clipping-safe margins and no extra objects or tasks; the Neo-Brutalism sample uses two-pixel borders and crisp four-pixel offset shadows.
- Replaced the short plugin note with a detailed bilingual plugin development guide covering Manifest fields, lifecycle, the complete Lua API, runtime limits, Link behavior, packaging, installation, testing, troubleshooting, and release checks.
- Fixed `.pap` installation resetting the device: the measured `pap_install` call chain overflowed its 4 KiB stack while entering FATFS and cJSON. The worker now has a 6 KiB measured budget and reports its minimum reserve; registry and theme manifest buffers moved off the system-task stack.
- Expanded the bundled Counter plug-in into a direct-key interaction demo: UP increments, DOWN decrements, OK clears, UP/DOWN double-click changes by two, and the value is bounded to -9999…9999, with no persistent state or background task.
- Shortened the button multi-click window from 180 ms to 100 ms and system Home hold from 1.5 s to 800 ms. Native UP/DOWN double-clicks now move two rows instead of disappearing. Settings moves the public device code into a dedicated Device Info page and removes the theme footer; Plug-in Manager shows the code above the installed-app list. Added a dependency-free Web Bluetooth `.pap` installer with pairing-code input, Service UUID discovery, post-connect device-code verification, progress/error states, bounded acknowledged writes, protocol host tests, and service-UUID advertising for browser discovery.
- Rebuilt the native Settings app around four direct, persistent controls: 50%-default display brightness, 30%-default system volume with asynchronous preview, a 30-second-default screen timeout, and a key-sound switch that defaults off. A single bounded worker coalesces NVS writes and lazily initializes audio; the first key sequence after screen-off now wakes without activating the hidden UI. Linking the real ES8311/I2S path brings the final application image to 1,203,424 bytes, still leaving 62% of the factory partition free.
- Fixed blank UI text by enabling the LVGL decoder required by the generated RLE-compressed font; static validation and component configuration now reject a compressed-font/decoder mismatch.
- Replaced the incomplete built-in CJK subset with a reproducible 14 px / 4 bpp Noto Sans SC font covering all 3,755 GB2312 level-one common ideographs and two Font Awesome navigation icons; 16 alpha levels remove the visible 2 bpp edge quantization, while static checks lock the source graph, profile, icon range, and decoder.
- Redesigned the bottom action hints around the real input model: the system owns the UP/DOWN selection slot and button prefixes, plug-ins provide only short-OK and long-OK action nouns through `passport.ui.actions`, long labels are ellipsized, and long-OK remains system Home. Native plugin details and the counter sample now follow the same selection pattern.
- Reduced the pre-settings application image from 1,346,800 to 1,146,528 bytes (200,272 bytes, 14.9%) while expanding and smoothing Chinese coverage. The 4 bpp profile adds 101,936 bytes over the interim 16 px / 2 bpp build but does not add an LVGL buffer, task, font fallback, or kerning table; size optimization, the RGB565/Label/Flex-only LVGL paths, tightened dependencies, and the reduced BLE feature set remain enabled.
- Simplified the tracked repository root: moved GitHub-recognized community documents into `.github/`, moved the changelog into `docs/`, updated every reference, and added a root-document allowlist to repository checks.
- Repository-wide language policy: every maintained Markdown default `.md` file is English, Simplified Chinese uses a paired `.zh_CN.md`, and both provide language switches. Static checks reject missing peers, missing switches, and Chinese prose in English defaults.
- Phase one of the AI development workflow: streamlined task-based context routing, unified local/CI validation, added PR checks and a template, and committed the dependency lock for reproducible builds.
- PR review fixes: pinned GitHub Actions to full commit SHAs, split build/release jobs by least privilege, disabled persisted sync checkout credentials, added Feature Request and Usage Question forms, clarified private security-report fallback, and corrected stale README, CI-trigger, and branch descriptions.
- Changed commit titles, PR titles, and PR bodies from Chinese-default to English; updated the Chinese punctuation rule so it no longer applies to PR descriptions.
- Reworked `build-firmware.yml` to pass `SDKCONFIG_DEFAULTS=sdkconfig.defaults`, enable `partitions.csv`, preserve the 8 MB image header, merge a flashable `FoloToy-AI-Passport-full.bin`, publish only that artifact, and use Actions cache v5.
- Integrated upstream PR #6 to resolve PR #4 conflicts: Wi-Fi, Bluetooth LE, radio lifecycle, and low-power demos; a 3 MB factory partition; build/menu/configuration updates; hardware-guide coverage; and bilingual capability tables.
- Defined English imperative Conventional Commit formatting for both commits and PR titles.
- Removed stale sync-workflow template comments and generalized an irrelevant Redis TTL rule to cache components.
- Added Chinese punctuation, credential safety, and recoverable file-deletion conventions.
- Expanded source-comment requirements for functions, state, ownership, concurrency, timing, registers, and magic values.
- Removed AI execution instructions from product READMEs so they remain human-facing product and repository overviews.
- Added `docs/development/agent-guide.md` as the focused AI workflow guide.
- Updated `AGENTS.md`, `docs/INDEX.md`, and the development index for the agent guide.
- Documented why the root README path is reserved for fork owners and how GitHub README precedence supports it.
- Created `main-update` from the upstream-aligned baseline and combined the repository-structure, firmware-CI, and upstream-sync work.
- Corrected the merged documentation index, workflow path, project tree, and CI references.
- Moved CI documentation from software design to `docs/development/`.
- Moved fork-only documentation assets from `assets/docs/` to `docs/assets/`.
- Moved the upstream English/Chinese project READMEs under `docs/` and renamed the documentation catalog to `docs/INDEX.md`.
- Initialized `AGENTS.md`, `CLAUDE.md`, and `CHANGELOG.md`.
- Standardized the initial project README language filenames.
- Added the `docs/`, `assets/`, and `skills/` directory structure.
- Moved the upstream hardware guide into `docs/hardware-design/`.
- Standardized subdirectory README capitalization and introduced fork conventions.
- Allowed fork-owned root README and supplemental documentation content on fork `main`.
- Added and documented the fork-only supplemental-document directory.
- Moved the build CI document to its dedicated CI branch before consolidation.
- Documented clean-`main` reasons, the direct-development exception, and Actions enablement for forks.
- Split the original agent rules into contribution, development, and fork documents with a compact root index.
- Updated software-design and project README references for the new documentation structure.
- Added the documentation catalog and task-triggered routing based on the earlier repository model.
- Added bilingual contribution, code-of-conduct, security, and support documents tailored to this ESP-IDF and fork workflow.

## Passport Platform v1 (this rewrite)

- Replaced the hard-coded demo menu with a Chinese launcher and single-foreground app model.
- Added installable `.pap` Lua plugins, plugin management, unpaired BLE installation, and public device-code target checks.
- Added a shared Chinese page container, status bar, semantic action-hint bar, and one 14 px / 4 bpp system font.
- Added lightweight theme tokens installable through the same `.pap` and BLE path.
- Added a counter plugin sample, night theme sample, package tools, BLE install tool, and platform documentation.

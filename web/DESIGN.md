<p align="right">
  <a href="DESIGN.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Passport Web Tool

## Purpose

The page is a local utility for one nearby Passport over Web Bluetooth. A shared connection surface appears first, followed by the Passport Installer and the 2FA key sender. The installer sends a local plug-in or theme `.pap`; the sender provisions one TOTP account to the 2FA Authenticator PAP. Selected packages, account records, and secrets move only between the browser and device and are never uploaded. The user enters the public device code shown on the Passport; the browser discovers devices by the Passport Service UUID in the primary advertisement, reads the public Device Code from GATT after connecting, and rejects a mismatched target.

## Run locally

Web Bluetooth requires a secure context. Localhost is treated as secure, so serve the directory instead of opening the HTML file directly:

```bash
python3 -m http.server 8000 --directory web
```

Open `http://localhost:8000/installer.html` in desktop Chrome or Edge. Safari, Firefox, and iOS browsers do not currently expose Web Bluetooth.

## Interaction and visual system

The page starts with device targeting and connection state, then presents two numbered task surfaces. A verified connection is reused instead of asking the user to reconnect for each task. The pairing field canonicalizes input as uppercase `XXXXX-XXXXX-X` while typing, so pasted, spaced, or already-separated codes follow one validation path. The installer keeps file, readiness, progress, device feedback, failure recovery, and completion visible. The 2FA sender accepts either an `otpauth://totp` URI or explicit issuer, account, Base32 secret, digits, and period fields; it keeps the secret masked by default and provides explicit time-sync and send actions. Neither flow uses app-owned dialogs.

Web Bluetooth's browser-owned authorization prompt remains mandatory for the site's first device access. On subsequent attempts, `navigator.bluetooth.getDevices()` provides previously authorized devices, and an authorized device whose advertised name exactly matches `Passport-<code>` reconnects directly without the picker; otherwise, the browser lists devices advertising the Passport Service. The page verifies the full device code over GATT before enabling either task. GitHub Pages deploys this same page at the root without a package catalog, separate theme route, remote package preload, or bundled PAP files. Keyboard focus, drag-and-drop, invalid-code, disabled, working, success, error, disconnected, unsupported-browser, and insecure-context states are explicit.

The visual language extends the existing Passport installer: a light-blue field, dark ink, restrained blue controls, one crisp bordered surface, 12–16 px radii, and neutral offset shadows. System fonts avoid a web-font download on a local provisioning tool. SVG icons share one stroke style; state is never communicated by color alone.

## 2FA provisioning

The sender targets the fixed manifest namespace `com.folotoy.totp-authenticator`. Before adding an account, the device must show **2FA Authenticator → Receive 2FA key**. Parsing an `otpauth://totp` URI fills the visible fields, supports SHA-1 with 6 or 8 digits, and rejects malformed or unsupported inputs before any BLE write. Sending an account also sends the browser's current Unix time. A separate synchronization action updates time without changing stored accounts.

The page waits for the PAP's Link response and reports success only after the plugin confirms its private-storage write. On success, the URI and secret fields are cleared. The current Link transport is intentionally unencrypted and unauthenticated; the page states that boundary next to the secret action instead of implying that the public device code protects confidentiality.

## Protocol and limits

The page accepts non-empty `.pap` files up to 4 MiB, validates and canonicalizes the entered device code, discovers the custom service UUID from the primary advertisement, verifies the GATT-reported code, computes IEEE CRC-32 locally, and streams 180-byte chunks with acknowledged writes and bounded retries. Service-based discovery preserves the proven installer behavior from firmware commit `9d27ed8` and does not depend on a browser merging the primary advertisement with its scan response before filtering. It waits for the device's start and final status notifications.

2FA application messages use complete Passport Link frames over the existing Link RX/TX characteristics and remain within the 200-byte payload bound. The page uses a random public 48-bit browser source ID for the connection, the verified Passport code as target, and the FNV-1a service ID derived from the fixed PAP manifest ID. Pure helpers are covered by `tests/test_web_installer_protocol.mjs`, `tests/test_passport_auth_protocol.mjs`, and `tests/test_passport_totp_protocol.mjs`, all included in the repository static gate.

<p align="right">
  <a href="DESIGN.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Passport Web Installer

## Purpose

The installer is a local utility for sending one `.pap` package from a desktop browser to one nearby Passport over Web Bluetooth. It never uploads the selected package. The user enters the public pairing code shown on the Passport; the browser discovers devices by the Passport Service UUID in the primary advertisement, while the page reads the public Device Code from GATT after connecting and rejects a mismatched target.

## Run locally

Web Bluetooth requires a secure context. Localhost is treated as secure, so serve the directory instead of opening the HTML file directly:

```bash
python3 -m http.server 8000 --directory web
```

Open `http://localhost:8000/installer.html` in desktop Chrome or Edge. Safari, Firefox, and iOS browsers do not currently expose Web Bluetooth.

## Interaction and visual system

The page uses one linear task surface: choose a `.pap` package, enter the pairing code, then install. The pairing field canonicalizes input as uppercase `XXXXX-XXXXX-X` while typing, so pasted, spaced, or already-separated codes follow one validation path. File, target, readiness, progress, device response, failure recovery, and completion stay visible without app-owned dialogs. Web Bluetooth's browser-owned authorization prompt remains mandatory for the site's first device access. On subsequent attempts, `navigator.bluetooth.getDevices()` provides previously authorized devices, and an authorized device whose advertised name exactly matches `Passport-<code>` reconnects directly without the picker; otherwise, the browser lists devices advertising the Passport Service. The page verifies the full pairing code over GATT before enabling installation. Pages cards pass a same-origin package URL in the query string; the installer fetches it locally and treats it exactly like a selected file. Keyboard focus, drag-and-drop, invalid-code, disabled, working, success, error, disconnected, unsupported-browser, and insecure-context states are explicit.

The visual language extends the existing Passport installer: a light-blue field, dark ink, restrained blue controls, one crisp bordered surface, 12–16 px radii, and neutral offset shadows. System fonts avoid a web-font download on a local provisioning tool. SVG icons share one stroke style; state is never communicated by color alone.

## Agent authorization demo

The authorization demo extends the same local Passport web tool with a second, live-monitor workflow. It assumes the Agent authorization panel is already open on the device, then keeps connection, editable request, exact payload preview, response, and event history in one surface. The request editor is intentionally dense because the important decision is the payload itself; the monitor column gives connection and delivery state a stable visual home.

The demo inherits the installer palette and type scale: light-blue canvas, dark ink, restrained blue primary action, neutral elevation, and system fonts. Its main states are disconnected, connecting, connected, sending, response received, and error. The Send request action is the strongest control; Connect device and Cancel current request remain visibly secondary. The mobile layout collapses to one column while preserving the payload preview before the send action.

## Protocol and limits

The page accepts non-empty `.pap` files up to 4 MiB, validates and canonicalizes the entered pairing code, discovers the custom service UUID from the primary advertisement, verifies the GATT-reported code, computes IEEE CRC-32 locally, and streams 180-byte chunks with acknowledged writes and bounded retries. Service-based discovery preserves the proven installer behavior from firmware commit `9d27ed8` and does not depend on a browser merging the primary advertisement with its scan response before filtering. It waits for the device's start and final status notifications. Pure protocol helpers are covered by `tests/test_web_installer_protocol.mjs` and the repository static gate.

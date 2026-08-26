<p align="right">
  <a href="agent-authorization.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Agent Authorization Panel

The Agent authorization panel is a foreground Lua app packaged as a PAP. An Agent-side plugin or bridge sends a compact JSON request through Passport Link. The panel displays the request and its options; the user confirms one option, and the panel sends a compact JSON response back through the same Link connection.

The current implementation intentionally keeps the integration small:

- The panel must already be open and remain the foreground app.
- The transport uses the existing Passport Link message frame.
- The application payload is compact JSON encoded as UTF-8.
- The example does not require a deny or cancel option.
- The only addressing check is the matching device code. This is not an authentication or authorization boundary.

## PAP example

The example source is in examples/agent-auth-panel:

~~~text
examples/agent-auth-panel/
├── manifest.json
├── main.lua
└── README.md
~~~

Build it with:

~~~bash
python3 tools/pack_pap.py examples/agent-auth-panel examples/agent-auth-panel/dist/agent-auth-panel.pap
~~~

The example manifest currently uses com.folotoy.agent-auth. The ID is not a protocol-wide requirement. Passport Link derives its service namespace from the manifest ID, so the Agent side must use the same ID when it changes.

## Application payloads

All payloads contain v=1. Requests use kind=request:

~~~json
{"v":1,"kind":"request","rid":"demo-001","title":"Run command","message":"Run npm test?","options":[["once","Run once"],["always","Always allow"],["cancel","Cancel"]]}
~~~

Field rules implemented by the panel:

- rid: one to 24 bytes, identifying one request from one source.
- title: one to 24 bytes.
- message: one to 72 bytes.
- options: one to three pairs. Each pair is option ID and display label.
- option ID: one to 16 bytes.
- option label: one to 18 bytes.
- Text is UTF-8 JSON text without control characters, quotes, or backslashes.
- The complete payload must be at most 200 bytes, matching the current Link payload limit.

The panel selects the first option by default. UP and DOWN move through the options; a double press moves by two positions. OK sends a response:

~~~json
{"v":1,"kind":"response","rid":"demo-001","status":"selected","option":"once"}
~~~

The panel also uses these status values when it must report a transport-level condition:

~~~text
busy       The panel is displaying another request.
conflict   The same request ID was received with different content.
cancelled  The panel is leaving the current request without selecting it.
~~~

Only selected responses include option. A cancel message can ask the panel to leave a matching request:

~~~json
{"v":1,"kind":"cancel","rid":"demo-001"}
~~~

Unknown or malformed application payloads are ignored. A duplicate request with the same source code, request ID, and exact payload replays the cached response. This lets a sender retry a lost response without showing a second prompt. A changed payload with the same source and request ID produces conflict.

## Passport Link addressing

The Link frame carries source device ID, target device ID, service ID, sequence, and the application payload. The service ID is the FNV-1a hash of the UTF-8 manifest ID. For the example manifest, the hash is 0x7e22d01e.

The Web Bluetooth characteristics used by the demo are:

~~~text
Link RX  01000000-0058-5254-524f-505353415031
Link TX  01000000-0058-5454-524f-505353415031
~~~

The panel receives only messages addressed to its current service namespace. The Web Demo sends the panel's device code as target and the editable source code as source. It listens for response notifications on the existing Link TX characteristic.

This implementation does not add a pairing step, signature, encryption, replay protection beyond request-ID handling, or stronger peer authentication. The device code is an address, not a secret.

## Web Demo

Open web/agent-auth-demo.html from a local HTTP server. It connects to an already-open panel over Web Bluetooth and provides editable fields for:

- target device code;
- source device code;
- manifest/service ID;
- request ID, title, message, and up to three options.

The page shows the exact JSON payload and byte count before sending. It also supports sending a cancel message, receiving and parsing panel responses, and keeping an event log. The page uses the same UUIDs and little-endian Link frame layout as the firmware.

Because Web Bluetooth is a browser permission flow, the page should be served from localhost or another permitted secure origin. The Passport device must be nearby, connected, and subscribed to the Link characteristics.

<p align="right"><strong>English</strong> · <a href="README.zh_CN.md">简体中文</a></p>

# Agent authorization panel

This installable Lua app displays one authorization request from an external
Agent client, lets the user move through up to three options, and sends the
selected option back over Passport Link.

The app must be open in the foreground before the Agent sends a request. It
does not add authentication; the existing Passport Link device-code target
check is the only addressing check.

Build and inspect the package:

~~~bash
python3 tools/pack_pap.py examples/agent-auth-panel examples/agent-auth-panel/dist/agent-auth-panel.pap
python3 tools/inspect_pap.py examples/agent-auth-panel/dist/agent-auth-panel.pap
~~~

The application payload is a compact JSON request:

~~~json
{"v":1,"kind":"request","rid":"a-001","title":"Run command","message":"Run npm test?","options":[["once","Run once"],["cancel","Cancel"]]}
~~~

The Link payload must remain at most 200 UTF-8 bytes. Text fields cannot
contain control characters; quotes and backslashes use normal JSON escaping
and are decoded by the shared `passport.json` system API. A duplicate request
with the same source code, request ID, and normalized content replays the last
result without asking again; this is a delivery retry aid, not a security
feature.

<p align="right">
  <a href="totp-authenticator.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# 2FA Authenticator PAP

The 2FA Authenticator is an installable Lua PAP in `examples/totp-authenticator`. It receives TOTP account records from the shared GitHub Pages tool over Passport Link, stores them in the app's private persistent storage, and generates RFC 6238 codes on the device.

The responsibility boundary is intentional:

- Firmware provides only the generic volatile `passport.clock` and route-scoped `passport.app.on_tick` APIs.
- The PAP implements Base32 decoding, HMAC-SHA1, the moving counter, dynamic truncation, account validation, persistence, and the 2FA UI.
- The browser supplies its current Unix time and the account record over the existing plaintext Passport Link connection.

## Install and provision

1. Build the package with `python3 tools/pack_pap.py examples/totp-authenticator examples/totp-authenticator/dist/totp-authenticator.pap`.
2. Open the GitHub Pages tool, enter the device code at the top, and connect once.
3. Use **Passport Installer** to install the local `.pap`.
4. Open **2FA Authenticator → Receive 2FA key** on the device.
5. In **2FA key sender**, paste an `otpauth://totp` URI or enter the issuer, account, Base32 secret, digits, and period, then send it.

The sender includes the browser's current Unix time with every account. A separate **Sync time** action is available after the account already exists. The device must keep the receive page open while adding an account; a time-only message is accepted whenever the PAP is in the foreground.

## Application payloads

All payloads are compact UTF-8 JSON within the Passport Link 200-byte limit. An account request is:

~~~json
{"v":1,"k":"add","q":"a01","i":"Example","a":"alice@example.com","s":"JBSWY3DPEHPK3PXP","d":6,"p":30,"t":"1730000000"}
~~~

Field rules are:

- `v`: exactly `1`.
- `k`: `add` for an account or `time` for time-only synchronization.
- `q`: three lowercase base-36 characters; the PAP echoes this request tag so a delayed response cannot acknowledge a newer send.
- `i`: issuer, zero to 24 UTF-8 bytes.
- `a`: account label, one to 48 UTF-8 bytes; issuer and account together are at most 52 bytes.
- `s`: normalized unpadded Base32 secret, 16 to 64 characters using `A-Z2-7`.
- `d`: 6 or 8 digits.
- `p`: integer period from 15 to 120 seconds.
- `t`: decimal Unix-seconds string between 2024-01-01 and 9999-12-31 UTC.

A time-only request is:

~~~json
{"v":1,"k":"time","q":"a02","t":"1730000000"}
~~~

The PAP sends `{"v":1,"k":"added","q":"a01"}` only after the atomic storage write succeeds, and `{"v":1,"k":"time","q":"a02"}` after a valid time synchronization. Failures use `{"v":1,"k":"error","q":"a01","e":"code"}`. Unknown fields, malformed Base32, unsupported algorithms, out-of-range values, and oversized payloads are rejected.

Sending the same issuer/account pair replaces the stored record. A new identity is rejected after 12 accounts. The PAP never logs or returns a stored secret. If the persisted state is malformed or unreadable, it reports the failure and does not silently replace the existing file.

## Time and code generation

The board has no battery-backed wall clock. `passport.clock.sync` anchors the supplied Unix time to the ESP32-C3 monotonic timer, so it remains available across PAP restarts while firmware stays powered. Rebooting or removing power invalidates it; use **Sync time** again before trusting a code.

TOTP generation remains in Lua. The PAP decodes the Base32 secret, creates the 64-bit big-endian time-step counter without relying on a 64-bit Lua number, calculates HMAC-SHA1, applies RFC 4226 dynamic truncation, and displays the configured 6- or 8-digit code with a one-second countdown.

## Transport and security boundary

The manifest ID is `com.folotoy.totp-authenticator`; Passport Link derives the service namespace from that ID. The browser writes Link frames to the existing Link RX characteristic and waits for responses on Link TX. The public device code is checked before delivery to reduce accidental transmission to the wrong Passport.

Passport Link v1 deliberately has no BLE pairing, application encryption, or peer authentication. The 2FA secret therefore crosses BLE in plaintext and a nearby observer or writer is outside this feature's protection. Private PAP storage is isolated from other apps, but it is FAT storage rather than hardware-backed secure storage.

Host coverage lives in `tests/test_totp_authenticator_plugin.lua` and `tests/test_passport_totp_protocol.mjs`. Real-device acceptance still needs installation, account provisioning, persistence across PAP restarts, time resynchronization after reboot, and comparison against a known RFC 6238 implementation.

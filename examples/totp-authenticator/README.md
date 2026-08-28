<p align="right"><strong>English</strong> · <a href="README.zh_CN.md">简体中文</a></p>

# 2FA authenticator

This installable Lua PAP receives TOTP account records from the unified
Passport web tool over Passport Link, stores them in the App's private
persistent storage, and generates RFC 6238 codes on the device. Base32
decoding, HMAC-SHA1, counter construction, and dynamic truncation all live in
the PAP; firmware only supplies a volatile wall clock and a route-scoped tick.

Open **Receive key** in the PAP before sending an account from the web page.
The v1 payload supports 6- or 8-digit SHA-1 TOTP with a 15–120 second period.
Sending the same issuer/account pair replaces its stored secret. Up to 12
accounts are retained.

The current board has no battery-backed wall clock. After a reboot or power
loss, open the PAP and use the web page's **Sync time** action before reading
codes. Time remains valid while the firmware stays powered, including after
leaving and reopening the PAP.

The existing Passport Link v1 connection is intentionally unencrypted and
unauthenticated. The public device code checks the destination but does not
protect a TOTP secret from a nearby observer or writer. The PAP never logs or
returns stored secrets, but its private FAT storage is not hardware-backed
secure storage.

Build and inspect the package:

~~~bash
python3 tools/pack_pap.py examples/totp-authenticator examples/totp-authenticator/dist/totp-authenticator.pap
python3 tools/inspect_pap.py examples/totp-authenticator/dist/totp-authenticator.pap
~~~

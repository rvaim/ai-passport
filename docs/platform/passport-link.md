<p align="right"><strong>English</strong> · <a href="passport-link.zh_CN.md">简体中文</a></p>

# Passport Link v1

## Device code

The device code encodes the ESP32-C3 factory MAC's unique 48-bit value as ten Base32 characters and adds one typing-check character, for example `22222-22222-2`. The installer presents this value as the pairing code. It is public and shareable, and is used only for discovery, addressing, and misdelivery prevention; it is not a password or authentication secret.

The complete BLE name is returned in scan response data:

```text
Passport-22222-22222-2
```

The primary advertisement includes the 128-bit Passport service UUID so Web Bluetooth service filters can discover the device. A client must still read the Device Code characteristic after connecting and use that value as the target check.

## App message frames

The fixed header is 36 bytes:

| Field | Size | Meaning |
| --- | ---: | --- |
| magic | 2 | `PL` |
| version | 1 | `1` |
| type | 1 | message/file/stream |
| source_id | 8 | public source device ID |
| target_id | 8 | must match this device before delivery |
| service | 4 | FNV-1a of the app manifest ID |
| sequence | 4 | sender sequence number |
| payload_len | 2 | at most 200 bytes in v1 |
| reserved | 2 | zero |
| payload_crc32 | 4 | IEEE CRC-32 |

The system validates version, length, CRC, and target before delivering a frame to the current app namespace.

## BLE package installation

```text
Service      0100004b-4e49-4c54-524f-505353415031
Device Code  01000045-444f-4354-524f-505353415031
Package Ctrl 01000043-474b-5054-524f-505353415031
Package Data 01000044-474b-5054-524f-505353415031
Package Stat 01000053-474b-5054-524f-505353415031
```

The little-endian begin control value is `op:u8=1 + total_size:u32 + crc32:u32 + target_id:u64`. The system compares `target_id` before opening the staging file. Subscribe to Package Stat first, write begin with a response, and wait for the receiver-ready status. Write package bytes to Package Data in 180-byte acknowledged chunks, then write the single byte `0x02` to Package Ctrl and wait for the installation-success or a failure status. Acknowledged data writes are required backpressure for the receiver's bounded queue; clients must not treat transmission completion as installation success.

NimBLE callbacks only copy bounded chunks into a fixed queue. The `pap_install` worker owns file writes, streaming CRC, package validation, and installation.

The dependency-free [Web installer](../../web/installer.html) implements this flow. Serve `web/` from HTTPS or localhost, choose a `.pap`, and enter the pairing code in desktop Chrome or Edge. The browser picker discovers advertisements containing the Passport Service UUID; after connecting, the page reads the Device Code characteristic and requires the full code to match. Web Bluetooth requires an explicit browser authorization prompt the first time a site accesses a device; the page can reconnect a previously authorized matching device without opening the picker. The command-line alternative remains `tools/ble_install.py`.

## Security boundary

- BLE system pairing and bonding are intentionally disabled.
- The device code is public and must not be treated as a password.
- V1 does not prevent eavesdropping or a malicious nearby writer; its target check only reduces accidental delivery to the wrong device.
- Package Service still enforces size, CRC, manifest, and path-traversal constraints before committing files.

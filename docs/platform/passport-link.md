<p align="right"><strong>English</strong> · <a href="passport-link.zh_CN.md">简体中文</a></p>

# Passport Link v1

The public device code is derived from the 48-bit factory MAC and includes a typing checksum. It is an address, not a password. BLE uses no system pairing.

App frames carry a 36-byte header with version, source ID, target ID, app service hash, sequence number, payload length, and CRC-32. The system drops frames whose target ID does not match this device.

Package installation uses separate control, data, and status GATT characteristics. Data is queued in bounded chunks and written by a worker task before Package Service validates and installs the `.pap`.

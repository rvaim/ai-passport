import assert from "node:assert/strict";

import {
  MAX_PACKAGE_SIZE,
  PASSPORT_SERVICE_UUID,
  advertisedNameForDeviceCode,
  crc32,
  formatDeviceCode,
  makeBeginControl,
  makeDeviceRequestOptions,
  parseDeviceCode,
} from "../web/passport-install-protocol.mjs";

for (const deviceId of [0n, 1n, 0x123456789abcn, 0xffffffffffffn]) {
  const code = formatDeviceCode(deviceId);
  assert.match(code, /^[23456789A-HJ-NP-Z]{5}-[23456789A-HJ-NP-Z]{5}-[23456789A-HJ-NP-Z]$/);
  assert.equal(parseDeviceCode(code), deviceId);
  assert.equal(parseDeviceCode(code.toLowerCase().replaceAll("-", "")), deviceId);
}

assert.throws(() => parseDeviceCode("22222-22222-3"), /校验失败/);
assert.equal(advertisedNameForDeviceCode("22222222222"), "Passport-22222-22222-2");
assert.throws(() => advertisedNameForDeviceCode("22222-22222-3"), /校验失败/);
assert.deepEqual(makeDeviceRequestOptions(), {
  filters: [{ services: [PASSPORT_SERVICE_UUID] }],
});
assert.equal(crc32(new TextEncoder().encode("123456789")), 0xcbf43926);

const targetId = 0x123456789abcn;
const control = makeBeginControl(0x00020304, 0xa1b2c3d4, targetId);
assert.equal(control.length, 17);
assert.deepEqual([...control.slice(0, 9)], [1, 4, 3, 2, 0, 0xd4, 0xc3, 0xb2, 0xa1]);
assert.deepEqual([...control.slice(9)], [0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0, 0]);
assert.throws(() => makeBeginControl(MAX_PACKAGE_SIZE + 1, 0, 0n), /大小无效/);
assert.match(PASSPORT_SERVICE_UUID, /^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$/);

console.log("Web installer protocol tests: PASS");

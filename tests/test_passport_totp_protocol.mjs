import assert from "node:assert/strict";

import {
  TOTP_APP_ID,
  TOTP_SERVICE_ID,
  buildTimeSync,
  buildTotpAccount,
  normalizeTotpSecret,
  parseOtpAuthUri,
  parseTotpResponse,
} from "../web/passport-totp-protocol.mjs";
import { serviceIdForManifest } from "../web/passport-link-protocol.mjs";

assert.equal(TOTP_APP_ID, "com.folotoy.totp-authenticator");
assert.equal(TOTP_SERVICE_ID, serviceIdForManifest(TOTP_APP_ID));
assert.equal(
  normalizeTotpSecret("jbsw y3dp-ehpk3pxp===="),
  "JBSWY3DPEHPK3PXP"
);
assert.throws(() => normalizeTotpSecret("NOT-BASE32-01"), /Base32/);

assert.deepEqual(
  parseOtpAuthUri(
    "otpauth://totp/Example%3Aalice%40example.com" +
    "?secret=JBSWY3DPEHPK3PXP&issuer=Example&digits=6&period=30"
  ),
  {
    issuer: "Example",
    account: "alice@example.com",
    secret: "JBSWY3DPEHPK3PXP",
    digits: 6,
    period: 30,
  }
);
assert.throws(
  () => parseOtpAuthUri(
    "otpauth://totp/Example:alice?secret=JBSWY3DPEHPK3PXP&algorithm=SHA256"
  ),
  /仅支持 SHA1/
);
assert.equal(
  parseOtpAuthUri(
    "otpauth://totp/Example:alice?secret=JBSWY3DPEHPK3PXP&period=45"
  ).period,
  45
);
assert.throws(
  () => parseOtpAuthUri(
    "otpauth://totp/Example:alice?secret=JBSWY3DPEHPK3PXP&digits=7"
  ),
  /位数/
);

const time = buildTimeSync({requestTag: "a01", unixSeconds: "1730000000"});
assert.equal(time.json, '{"v":1,"k":"time","q":"a01","t":"1730000000"}');

const account = buildTotpAccount({
  issuer: "Example",
  account: "alice@example.com",
  secret: "JBSWY3DPEHPK3PXP",
  digits: 6,
  period: 30,
  unixSeconds: "1730000000",
  requestTag: "a02",
});
assert.equal(
  account.json,
  '{"v":1,"k":"add","q":"a02","i":"Example","a":"alice@example.com","s":"JBSWY3DPEHPK3PXP","d":6,"p":30,"t":"1730000000"}'
);
assert.ok(account.payload.length <= 200);
const maximumAccount = buildTotpAccount({
  issuer: "A".repeat(4),
  account: "B".repeat(48),
  secret: "A".repeat(64),
  digits: 8,
  period: 120,
  unixSeconds: "253402300799",
  requestTag: "zzz",
});
assert.equal(maximumAccount.payload.length, 197);
assert.throws(
  () => buildTotpAccount({
    issuer: "A".repeat(24),
    account: "B".repeat(48),
    secret: "JBSWY3DPEHPK3PXP",
    requestTag: "a03",
  }),
  /合计/
);
assert.throws(
  () => buildTimeSync({requestTag: "TOO-LONG", unixSeconds: "1730000000"}),
  /请求标记/
);

assert.deepEqual(
  parseTotpResponse('{"v":1,"k":"added","q":"a02"}'),
  {v: 1, k: "added", q: "a02"}
);
assert.deepEqual(
  parseTotpResponse('{"v":1,"k":"error","q":"a02","e":"not_ready"}'),
  {v: 1, k: "error", q: "a02", e: "not_ready"}
);
assert.throws(
  () => parseTotpResponse('{"v":1,"k":"added","q":"a02","secret":"leak"}'),
  /未知字段/
);

console.log("Passport TOTP provisioning protocol host tests: PASS");

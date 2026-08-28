import { LINK_MAX_PAYLOAD, serviceIdForManifest } from "./passport-link-protocol.mjs";

export const TOTP_APP_ID = "com.folotoy.totp-authenticator";
export const TOTP_SERVICE_ID = serviceIdForManifest(TOTP_APP_ID);
export const TOTP_SECRET_MAX_BYTES = 64;

const textEncoder = new TextEncoder();

function byteLength(value) {
  return textEncoder.encode(value).byteLength;
}

function assertText(value, name, maximum, required = false) {
  if (typeof value !== "string" || (required && value.length === 0) ||
      byteLength(value) > maximum) {
    throw new Error(name + "长度不符合限制");
  }
  for (const character of value) {
    const code = character.codePointAt(0);
    if (code < 0x20 || code === 0x7f) {
      throw new Error(name + "包含不支持的控制字符");
    }
  }
}

function assertUnixSeconds(value) {
  const normalized = String(value ?? "");
  if (!/^\d{10,12}$/.test(normalized)) {
    throw new Error("设备时间格式无效");
  }
  const seconds = BigInt(normalized);
  if (seconds < 1704067200n || seconds > 253402300799n) {
    throw new Error("设备时间必须位于 2024..9999 年");
  }
  return normalized;
}

function assertRequestTag(value) {
  const normalized = String(value ?? "");
  if (!/^[0-9a-z]{3}$/.test(normalized)) {
    throw new Error("2FA 请求标记无效");
  }
  return normalized;
}

export function normalizeTotpSecret(value) {
  const normalized = String(value ?? "")
    .toUpperCase()
    .replace(/[\s-]/g, "")
    .replace(/=+$/, "");
  if (normalized.length < 16 || normalized.length > TOTP_SECRET_MAX_BYTES ||
      !/^[A-Z2-7]+$/.test(normalized)) {
    throw new Error("密钥必须是 16–64 位 Base32 字符");
  }

  let bits = 0;
  let bitCount = 0;
  let decodedBytes = 0;
  for (const character of normalized) {
    const code = character.charCodeAt(0);
    const digit = code >= 65 && code <= 90 ? code - 65 : code - 50 + 26;
    bits = (bits << 5) | digit;
    bitCount += 5;
    if (bitCount >= 8) {
      bitCount -= 8;
      decodedBytes += 1;
      bits &= bitCount === 0 ? 0 : (1 << bitCount) - 1;
    }
  }
  if ((bitCount > 0 && bits !== 0) || decodedBytes < 10) {
    throw new Error("Base32 密钥编码不完整");
  }
  return normalized;
}

export function parseOtpAuthUri(value) {
  let uri;
  try {
    uri = new URL(String(value ?? "").trim());
  } catch {
    throw new Error("otpauth URI 格式无效");
  }
  if (uri.protocol !== "otpauth:" || uri.hostname.toLowerCase() !== "totp") {
    throw new Error("只支持 otpauth://totp URI");
  }
  let label;
  try {
    label = decodeURIComponent(uri.pathname.replace(/^\/+/, ""));
  } catch {
    throw new Error("otpauth 账号标签编码无效");
  }
  if (!label) throw new Error("otpauth URI 缺少账号标签");

  const separator = label.indexOf(":");
  const labelIssuer = separator >= 0 ? label.slice(0, separator).trim() : "";
  const account = (separator >= 0 ? label.slice(separator + 1) : label).trim();
  const issuer = (uri.searchParams.get("issuer") ?? labelIssuer).trim();
  const algorithm = (uri.searchParams.get("algorithm") ?? "SHA1").toUpperCase();
  if (algorithm !== "SHA1") {
    throw new Error("当前 PAP 仅支持 SHA1 TOTP");
  }
  const digits = Number(uri.searchParams.get("digits") ?? "6");
  const period = Number(uri.searchParams.get("period") ?? "30");
  return validateAccount({
    issuer,
    account,
    secret: normalizeTotpSecret(uri.searchParams.get("secret") ?? ""),
    digits,
    period,
  });
}

function validateAccount({ issuer, account, secret, digits, period }) {
  const normalized = {
    issuer: String(issuer ?? "").trim(),
    account: String(account ?? "").trim(),
    secret: normalizeTotpSecret(secret),
    digits: Number(digits),
    period: Number(period),
  };
  assertText(normalized.issuer, "签发方", 24);
  assertText(normalized.account, "账号", 48, true);
  if (byteLength(normalized.issuer) + byteLength(normalized.account) > 52) {
    throw new Error("签发方和账号合计不能超过 52 字节");
  }
  if (![6, 8].includes(normalized.digits)) {
    throw new Error("验证码位数必须是 6 或 8");
  }
  if (!Number.isInteger(normalized.period) ||
      normalized.period < 15 || normalized.period > 120) {
    throw new Error("验证码周期必须是 15–120 秒");
  }
  return normalized;
}

export function buildTimeSync({
  requestTag,
  unixSeconds = Math.floor(Date.now() / 1000),
}) {
  const json = JSON.stringify({
    v: 1,
    k: "time",
    q: assertRequestTag(requestTag),
    t: assertUnixSeconds(unixSeconds),
  });
  return { json, payload: textEncoder.encode(json) };
}

export function buildTotpAccount({
  issuer,
  account,
  secret,
  digits = 6,
  period = 30,
  unixSeconds = Math.floor(Date.now() / 1000),
  requestTag,
}) {
  const normalized = validateAccount({ issuer, account, secret, digits, period });
  const record = {
    v: 1,
    k: "add",
    q: assertRequestTag(requestTag),
    i: normalized.issuer,
    a: normalized.account,
    s: normalized.secret,
    d: normalized.digits,
    p: normalized.period,
    t: assertUnixSeconds(unixSeconds),
  };
  const json = JSON.stringify(record);
  const payload = textEncoder.encode(json);
  if (payload.length > LINK_MAX_PAYLOAD) {
    throw new Error("账号载荷超过 200 字节，请缩短签发方或账号");
  }
  return { ...normalized, json, payload };
}

export function parseTotpResponse(value) {
  const response = typeof value === "string" ? JSON.parse(value) : value;
  if (!response || response.v !== 1 ||
      !["added", "time", "error"].includes(response.k) ||
      !/^[0-9a-z]{3}$/.test(response.q ?? "")) {
    throw new Error("设备响应格式无效");
  }
  const keys = Object.keys(response).sort().join(",");
  if (response.k === "error") {
    if (keys !== "e,k,q,v" || typeof response.e !== "string" ||
        !/^[a-z_]+$/.test(response.e)) {
      throw new Error("设备错误响应无效");
    }
  } else if (keys !== "k,q,v") {
    throw new Error("设备响应包含未知字段");
  }
  return response;
}

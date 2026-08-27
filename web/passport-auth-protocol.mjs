import {
  DEVICE_CODE_UUID,
  PASSPORT_SERVICE_UUID,
  formatDeviceCode,
  parseDeviceCode,
} from "./passport-install-protocol.mjs";

export { DEVICE_CODE_UUID, PASSPORT_SERVICE_UUID, formatDeviceCode, parseDeviceCode };

export const LINK_RX_UUID = "01000000-0058-5254-524f-505353415031";
export const LINK_TX_UUID = "01000000-0058-5454-524f-505353415031";
export const LINK_PROTOCOL_VERSION = 1;
export const LINK_MESSAGE_TYPE = 1;
export const LINK_HEADER_SIZE = 36;
export const LINK_MAX_PAYLOAD = 200;

const DEVICE_ID_MAX = 0xffffffffffffn;
const textEncoder = new TextEncoder();

function byteLength(value) {
  return textEncoder.encode(value).byteLength;
}

function assertText(value, name, maximum, required = false) {
  if (typeof value !== "string" || (required && value.length === 0) ||
      byteLength(value) > maximum) {
    throw new Error(name + "长度不符合协议限制");
  }
  for (const character of value) {
    const code = character.codePointAt(0);
    if (code < 0x20 || code === 0x7f) {
      throw new Error(name + "包含协议不支持的字符");
    }
  }
}

function assertId(value, name, maximum) {
  if (typeof value !== "string" || value.length === 0 ||
      byteLength(value) > maximum || !/^[A-Za-z0-9._:-]+$/.test(value)) {
    throw new Error(name + "必须是 ASCII 标识");
  }
}

export function serviceIdForManifest(manifestId) {
  if (typeof manifestId !== "string" || !/^[a-z0-9._-]+$/.test(manifestId) ||
      manifestId.length === 0 || manifestId.length >= 48) {
    throw new Error("Manifest ID 无效");
  }
  let hash = 0x811c9dc5;
  for (const byte of textEncoder.encode(manifestId)) {
    hash ^= byte;
    hash = Math.imul(hash, 0x01000193);
  }
  return hash >>> 0;
}

export function buildAuthorizationRequest({ requestId, title, message, options }) {
  assertId(requestId, "请求 ID", 24);
  assertText(title, "标题", 24, true);
  assertText(message, "消息", 72);
  if (!Array.isArray(options) || options.length < 1 || options.length > 3) {
    throw new Error("选项数量必须为 1 到 3 项");
  }

  const seen = new Set();
  const normalized = options.map((option) => {
    if (!Array.isArray(option) || option.length !== 2) {
      throw new Error("选项格式无效");
    }
    const [id, label] = option;
    assertId(id, "选项 ID", 16);
    assertText(label, "选项文字", 18, true);
    if (seen.has(id)) throw new Error("选项 ID 不能重复");
    seen.add(id);
    return [id, label];
  });

  const json = JSON.stringify({
    v: 1,
    kind: "request",
    rid: requestId,
    title,
    message,
    options: normalized,
  });
  const payload = textEncoder.encode(json);
  if (payload.length > LINK_MAX_PAYLOAD) {
    throw new Error("请求 payload 超过 200 字节");
  }
  return { json, payload };
}

export function buildCancelRequest(requestId) {
  assertId(requestId, "请求 ID", 24);
  const json = JSON.stringify({ v: 1, kind: "cancel", rid: requestId });
  return { json, payload: textEncoder.encode(json) };
}

export function parseAuthorizationResponse(value) {
  const json = typeof value === "string" ? JSON.parse(value) : value;
  if (!json || json.v !== 1 || json.kind !== "response" ||
      typeof json.rid !== "string" || typeof json.status !== "string") {
    throw new Error("设备响应格式无效");
  }
  assertId(json.rid, "响应请求 ID", 24);
  if (json.status === "selected") {
    assertId(json.option, "响应选项", 16);
  } else if (!["cancelled", "busy", "invalid", "conflict"].includes(json.status)) {
    throw new Error("设备响应状态无效");
  }
  return json;
}

function writeUint64LE(view, offset, value) {
  if (typeof value !== "bigint" || value < 0n || value > DEVICE_ID_MAX) {
    throw new RangeError("Link 地址必须是 48-bit ID");
  }
  let remaining = value;
  for (let index = 0; index < 8; index += 1) {
    view.setUint8(offset + index, Number(remaining & 0xffn));
    remaining >>= 8n;
  }
}

function readUint64LE(view, offset) {
  let value = 0n;
  for (let index = 7; index >= 0; index -= 1) {
    value = (value << 8n) | BigInt(view.getUint8(offset + index));
  }
  return value;
}

export function encodeLinkMessage({ sourceId, targetId, serviceId, sequence, payload }) {
  if (!Number.isInteger(serviceId) || serviceId < 0 || serviceId > 0xffffffff) {
    throw new RangeError("service ID 无效");
  }
  if (!Number.isInteger(sequence) || sequence < 0 || sequence > 0xffffffff) {
    throw new RangeError("Link sequence 无效");
  }
  if (!(payload instanceof Uint8Array) || payload.length > LINK_MAX_PAYLOAD) {
    throw new RangeError("Link payload 超过 200 字节");
  }

  const bytes = new Uint8Array(LINK_HEADER_SIZE + payload.length);
  const view = new DataView(bytes.buffer);
  bytes[0] = 0x50;
  bytes[1] = 0x4c;
  bytes[2] = LINK_PROTOCOL_VERSION;
  bytes[3] = LINK_MESSAGE_TYPE;
  writeUint64LE(view, 4, sourceId);
  writeUint64LE(view, 12, targetId);
  view.setUint32(20, serviceId, true);
  view.setUint32(24, sequence, true);
  view.setUint16(28, payload.length, true);
  view.setUint16(30, 0, true);
  view.setUint32(32, crc32(payload), true);
  bytes.set(payload, LINK_HEADER_SIZE);
  return bytes;
}

export function decodeLinkMessage(value) {
  const bytes = value instanceof Uint8Array
    ? value
    : value instanceof ArrayBuffer
      ? new Uint8Array(value)
      : new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
  if (bytes.length < LINK_HEADER_SIZE) throw new Error("Link 帧长度不足");
  if (bytes[0] !== 0x50 || bytes[1] !== 0x4c || bytes[2] !== LINK_PROTOCOL_VERSION) {
    throw new Error("Link 帧版本无效");
  }
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const payloadLength = view.getUint16(28, true);
  if (payloadLength > LINK_MAX_PAYLOAD || bytes.length !== LINK_HEADER_SIZE + payloadLength) {
    throw new Error("Link payload 长度无效");
  }
  const payload = bytes.slice(LINK_HEADER_SIZE);
  if (crc32(payload) !== view.getUint32(32, true)) {
    throw new Error("Link payload CRC 无效");
  }
  return {
    type: bytes[3],
    sourceId: readUint64LE(view, 4),
    targetId: readUint64LE(view, 12),
    serviceId: view.getUint32(20, true),
    sequence: view.getUint32(24, true),
    payload,
  };
}

let crcTable;

function getCrcTable() {
  if (crcTable) return crcTable;
  crcTable = new Uint32Array(256);
  for (let index = 0; index < crcTable.length; index += 1) {
    let value = index;
    for (let bit = 0; bit < 8; bit += 1) {
      value = (value >>> 1) ^ (value & 1 ? 0xedb88320 : 0);
    }
    crcTable[index] = value >>> 0;
  }
  return crcTable;
}

export function crc32(bytes) {
  if (!(bytes instanceof Uint8Array)) throw new TypeError("CRC 输入必须是 Uint8Array");
  const table = getCrcTable();
  let value = 0xffffffff;
  for (const byte of bytes) value = table[(value ^ byte) & 0xff] ^ (value >>> 8);
  return (value ^ 0xffffffff) >>> 0;
}

import {
  DEVICE_CODE_UUID,
  PASSPORT_SERVICE_UUID,
  formatDeviceCode,
  parseDeviceCode,
} from "./passport-install-protocol.mjs";
import {
  LINK_HEADER_SIZE,
  LINK_MAX_PAYLOAD,
  LINK_MESSAGE_TYPE,
  LINK_PROTOCOL_VERSION,
  LINK_RX_UUID,
  LINK_TX_UUID,
  decodeLinkMessage,
  encodeLinkMessage,
  serviceIdForManifest,
} from "./passport-link-protocol.mjs";

export { DEVICE_CODE_UUID, PASSPORT_SERVICE_UUID, formatDeviceCode, parseDeviceCode };
export {
  LINK_HEADER_SIZE,
  LINK_MAX_PAYLOAD,
  LINK_MESSAGE_TYPE,
  LINK_PROTOCOL_VERSION,
  LINK_RX_UUID,
  LINK_TX_UUID,
  decodeLinkMessage,
  encodeLinkMessage,
  serviceIdForManifest,
};

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

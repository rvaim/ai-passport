import assert from "node:assert/strict";

import {
  LINK_HEADER_SIZE,
  LINK_MESSAGE_TYPE,
  LINK_MAX_PAYLOAD,
  LINK_RX_UUID,
  LINK_TX_UUID,
  buildAuthorizationRequest,
  buildCancelRequest,
  decodeLinkMessage,
  encodeLinkMessage,
  parseAuthorizationResponse,
  serviceIdForManifest,
} from "../web/passport-auth-protocol.mjs";

assert.equal(LINK_RX_UUID, "01000000-0058-5254-524f-505353415031");
assert.equal(LINK_TX_UUID, "01000000-0058-5454-524f-505353415031");

const request = buildAuthorizationRequest({
  requestId: "a-001",
  title: "执行命令",
  message: "是否执行 npm test",
  options: [
    ["once", "本次执行"],
    ["always", "始终允许"],
    ["cancel", "取消"],
  ],
});

assert.equal(
  request.json,
  '{"v":1,"kind":"request","rid":"a-001","title":"执行命令","message":"是否执行 npm test","options":[["once","本次执行"],["always","始终允许"],["cancel","取消"]]}'
);
assert.equal(request.payload.length, new TextEncoder().encode(request.json).length);
assert.ok(request.payload.length <= LINK_MAX_PAYLOAD);
assert.equal(serviceIdForManifest("com.folotoy.agent-auth"), 0x7e22d01e);

assert.throws(
  () => buildAuthorizationRequest({
    requestId: "a-001",
    title: "标题",
    message: "包含\n换行",
    options: [["allow", "允许"]],
  }),
  /不支持的字符/
);
assert.throws(
  () => buildAuthorizationRequest({
    requestId: "a-001",
    title: "标题",
    message: "消息",
    options: [["allow", "允许"], ["allow", "重复"]],
  }),
  /不能重复/
);

const cancel = buildCancelRequest("a-001");
assert.equal(cancel.json, '{"v":1,"kind":"cancel","rid":"a-001"}');

const sourceId = 0x001122334455n;
const targetId = 0x00aabbccddeen;
const serviceId = serviceIdForManifest("com.folotoy.agent-auth");
const frame = encodeLinkMessage({
  sourceId,
  targetId,
  serviceId,
  sequence: 7,
  payload: request.payload,
});
assert.equal(frame.length, LINK_HEADER_SIZE + request.payload.length);

const decoded = decodeLinkMessage(frame);
assert.equal(decoded.type, LINK_MESSAGE_TYPE);
assert.equal(decoded.sourceId, sourceId);
assert.equal(decoded.targetId, targetId);
assert.equal(decoded.serviceId, serviceId);
assert.equal(decoded.sequence, 7);
assert.deepEqual(decoded.payload, request.payload);

const corrupted = frame.slice();
corrupted[corrupted.length - 1] ^= 1;
assert.throws(() => decodeLinkMessage(corrupted), /CRC 无效/);

assert.deepEqual(
  parseAuthorizationResponse('{"v":1,"kind":"response","rid":"a-001","status":"selected","option":"once"}'),
  { v: 1, kind: "response", rid: "a-001", status: "selected", option: "once" }
);
assert.deepEqual(
  parseAuthorizationResponse('{"v":1,"kind":"response","rid":"a-001","status":"cancelled"}'),
  { v: 1, kind: "response", rid: "a-001", status: "cancelled" }
);
assert.throws(
  () => parseAuthorizationResponse('{"v":1,"kind":"response","rid":"a-001","status":"unknown"}'),
  /状态无效/
);
assert.throws(
  () => parseAuthorizationResponse('{"v":1,"kind":"response","rid":"bad id","status":"cancelled"}'),
  /必须是 ASCII 标识/
);

console.log("Passport auth protocol host tests: PASS");

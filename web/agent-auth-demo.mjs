import {
  DEVICE_CODE_UUID,
  LINK_RX_UUID,
  LINK_TX_UUID,
  LINK_MESSAGE_TYPE,
  PASSPORT_SERVICE_UUID,
  buildAuthorizationRequest,
  buildCancelRequest,
  decodeLinkMessage,
  formatDeviceCode,
  parseAuthorizationResponse,
  parseDeviceCode,
  serviceIdForManifest,
  encodeLinkMessage,
} from "./passport-auth-protocol.mjs";

const deviceCodeInput = document.querySelector("#device-code");
const sourceCodeInput = document.querySelector("#source-code");
const serviceIdInput = document.querySelector("#service-id");
const requestIdInput = document.querySelector("#request-id");
const requestTitleInput = document.querySelector("#request-title");
const requestMessageInput = document.querySelector("#request-message");
const payloadJson = document.querySelector("#payload-json");
const payloadBytes = document.querySelector("#payload-bytes");
const responseBox = document.querySelector("#response-box");
const responseSummary = document.querySelector("#response-summary");
const responseJson = document.querySelector("#response-json");
const eventLog = document.querySelector("#event-log");
const connectionState = document.querySelector("#connection-state");
const connectionLabel = document.querySelector("#connection-label");
const connectionDetail = document.querySelector("#connection-detail");
const statusBox = document.querySelector("#status");
const statusTitle = document.querySelector("#status-title");
const statusDetail = document.querySelector("#status-detail");
const connectButton = document.querySelector("#connect-button");
const sendButton = document.querySelector("#send-button");
const cancelButton = document.querySelector("#cancel-button");
const copyButton = document.querySelector("#copy-button");
const clearLogButton = document.querySelector("#clear-log-button");
const requestForm = document.querySelector("#request-form");

const textDecoder = new TextDecoder("utf-8");
const bluetoothAvailable = window.isSecureContext && Boolean(navigator.bluetooth);

let selectedDevice = null;
let targetDeviceId = null;
let sourceDeviceId = null;
let currentServiceId = null;
let rxCharacteristic = null;
let txCharacteristic = null;
let linkSequence = 0;
let sending = false;
let pendingRequestId = null;
let currentRequest = null;
let permittedDevices = [];

if (bluetoothAvailable && typeof navigator.bluetooth.getDevices === "function") {
  navigator.bluetooth.getDevices()
    .then((devices) => { permittedDevices = devices; })
    .catch(() => { permittedDevices = []; });
}

function setStatus(kind, title, detail) {
  statusBox.dataset.kind = kind;
  statusTitle.textContent = title;
  statusDetail.textContent = detail;
}

function setConnectionState(kind, label, detail) {
  connectionState.dataset.kind = kind;
  connectionLabel.textContent = label;
  if (detail) connectionDetail.textContent = detail;
}

function addLog(label, value) {
  const item = document.createElement("li");
  const time = document.createElement("time");
  const detail = document.createElement("code");
  time.textContent = new Date().toLocaleTimeString("zh-CN", {
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
  });
  detail.textContent = label + "  " + value;
  item.append(time, detail);
  eventLog.prepend(item);
  while (eventLog.children.length > 12) eventLog.lastElementChild.remove();
}

function setResponse(kind, summary, raw) {
  responseBox.dataset.kind = kind;
  responseSummary.textContent = summary;
  responseJson.textContent = raw || "";
}

function optionInputs() {
  return [1, 2, 3].map((index) => ({
    enabled: document.querySelector("#option-enabled-" + index),
    id: document.querySelector("#option-id-" + index),
    label: document.querySelector("#option-label-" + index),
  }));
}

function syncOptionRows() {
  for (const option of optionInputs()) {
    option.id.disabled = !option.enabled.checked;
    option.label.disabled = !option.enabled.checked;
  }
}

function collectOptions() {
  return optionInputs()
    .filter((option) => option.enabled.checked)
    .map((option) => [option.id.value.trim(), option.label.value]);
}

function collectRequest() {
  return buildAuthorizationRequest({
    requestId: requestIdInput.value.trim(),
    title: requestTitleInput.value,
    message: requestMessageInput.value,
    options: collectOptions(),
  });
}

function readAddressFields() {
  const targetId = parseDeviceCode(deviceCodeInput.value);
  const sourceId = parseDeviceCode(sourceCodeInput.value);
  const serviceId = serviceIdForManifest(serviceIdInput.value.trim());
  return { targetId, sourceId, serviceId };
}

function refreshPreview() {
  syncOptionRows();
  try {
    currentRequest = collectRequest();
    payloadJson.textContent = currentRequest.json;
    payloadBytes.textContent = currentRequest.payload.length + " B / 200 B";
  } catch (error) {
    currentRequest = null;
    payloadJson.textContent = "无法生成请求\n" + error.message;
    payloadBytes.textContent = "— / 200 B";
  }
  updateControls();
}

function isConnected() {
  return Boolean(
    selectedDevice?.gatt?.connected &&
    rxCharacteristic &&
    txCharacteristic &&
    targetDeviceId !== null &&
    sourceDeviceId !== null &&
    currentServiceId !== null
  );
}

function updateControls() {
  let addressValid = false;
  try {
    readAddressFields();
    addressValid = true;
  } catch {
    addressValid = false;
  }
  const connected = isConnected();
  connectButton.disabled = sending || !bluetoothAvailable || !addressValid;
  connectButton.textContent = connected ? "重新连接" : "连接设备";
  sendButton.disabled = sending || !connected || !currentRequest;
  cancelButton.disabled = sending || !connected || !pendingRequestId;
  copyButton.disabled = !currentRequest;
}

function clearConnection(announce = true) {
  if (txCharacteristic) {
    txCharacteristic.removeEventListener("characteristicvaluechanged", onLinkNotification);
  }
  if (selectedDevice) {
    selectedDevice.removeEventListener("gattserverdisconnected", onDisconnected);
  }
  txCharacteristic = null;
  rxCharacteristic = null;
  currentServiceId = null;
  targetDeviceId = null;
  sourceDeviceId = null;
  selectedDevice = null;
  pendingRequestId = null;
  if (announce) {
    setConnectionState("error", "已断开", "请重新连接 Passport。");
    setStatus("error", "设备连接已断开", "重新连接后可以继续发送授权请求。");
  }
  updateControls();
}

function disconnectCurrent() {
  if (selectedDevice?.gatt?.connected) {
    selectedDevice.removeEventListener("gattserverdisconnected", onDisconnected);
    selectedDevice.gatt.disconnect();
  }
  clearConnection(false);
}

function onDisconnected() {
  clearConnection(true);
  addLog("连接", "Passport 已断开");
}

async function connectDevice() {
  const addresses = readAddressFields();
  const pairingCode = formatDeviceCode(addresses.targetId);
  const advertisedName = "Passport-" + pairingCode;

  disconnectCurrent();
  setConnectionState("working", "连接中", "正在查找 " + advertisedName + "。");
  setStatus("working", "正在查找设备", "请选择 " + advertisedName + "；连接后会再次读取设备码。");
  connectButton.disabled = true;

  let candidate = permittedDevices.find((device) => device.name === advertisedName);
  if (!candidate) {
    candidate = await navigator.bluetooth.requestDevice({
      filters: [{ services: [PASSPORT_SERVICE_UUID] }],
    });
    if (!permittedDevices.some((device) => device.id === candidate.id)) {
      permittedDevices = [...permittedDevices, candidate];
    }
  }

  try {
    setConnectionState("working", "连接中", candidate.name || "正在建立蓝牙连接…");
    setStatus("working", "正在连接设备", candidate.name || "正在建立蓝牙连接…");
    const server = await candidate.gatt.connect();
    const service = await server.getPrimaryService(PASSPORT_SERVICE_UUID);
    const [codeCharacteristic, nextRx, nextTx] = await Promise.all([
      service.getCharacteristic(DEVICE_CODE_UUID),
      service.getCharacteristic(LINK_RX_UUID),
      service.getCharacteristic(LINK_TX_UUID),
    ]);
    const returnedCode = textDecoder.decode(await codeCharacteristic.readValue()).trim().toUpperCase();
    const returnedId = parseDeviceCode(returnedCode);
    if (returnedId !== addresses.targetId) {
      throw new Error("配对码与设备返回的设备码不一致（设备返回 " + returnedCode + "）");
    }
    await nextTx.startNotifications();
    nextTx.addEventListener("characteristicvaluechanged", onLinkNotification);

    selectedDevice = candidate;
    targetDeviceId = addresses.targetId;
    sourceDeviceId = addresses.sourceId;
    currentServiceId = addresses.serviceId;
    rxCharacteristic = nextRx;
    txCharacteristic = nextTx;
    selectedDevice.addEventListener("gattserverdisconnected", onDisconnected);
    setConnectionState("success", "已连接", "设备码复核通过，可以发送请求。");
    connectionDetail.textContent = "已连接 " + returnedCode + "；TX notification 已订阅。";
    setStatus("success", "设备已连接", "授权面板必须停留在设备前台，现在可以发送请求。");
    addLog("连接", returnedCode + "；service=" + serviceIdInput.value.trim());
  } catch (error) {
    if (candidate?.gatt?.connected) candidate.gatt.disconnect();
    throw error;
  } finally {
    updateControls();
  }
}

async function writeLinkFrame(frame) {
  if (!rxCharacteristic || !selectedDevice?.gatt?.connected) {
    throw new Error("设备连接已断开");
  }
  if (typeof rxCharacteristic.writeValueWithResponse === "function") {
    await rxCharacteristic.writeValueWithResponse(frame);
  } else if (typeof rxCharacteristic.writeValueWithoutResponse === "function") {
    await rxCharacteristic.writeValueWithoutResponse(frame);
  } else {
    await rxCharacteristic.writeValue(frame);
  }
}

function nextSequence() {
  linkSequence = linkSequence >= 0xffffffff ? 0 : linkSequence + 1;
  return linkSequence;
}

async function sendPayload(payload, label) {
  const addresses = readAddressFields();
  const frame = encodeLinkMessage({
    sourceId: addresses.sourceId,
    targetId: addresses.targetId,
    serviceId: addresses.serviceId,
    sequence: nextSequence(),
    payload,
  });
  await writeLinkFrame(frame);
  addLog(label, new TextDecoder().decode(payload));
}

async function sendRequest() {
  if (!isConnected()) throw new Error("请先连接 Passport");
  const request = collectRequest();
  const addresses = readAddressFields();
  if (addresses.serviceId !== currentServiceId ||
      addresses.targetId !== targetDeviceId ||
      addresses.sourceId !== sourceDeviceId) {
    throw new Error("设备地址或面板 ID 已改变，请重新连接");
  }

  sending = true;
  pendingRequestId = requestIdInput.value.trim();
  setResponse("idle", "等待设备回复", "");
  setStatus("working", "正在发送授权请求", "请确认设备仍停留在 Agent 授权面板。");
  updateControls();
  try {
    await sendPayload(request.payload, "发送 request");
    setStatus("success", "请求已发送", "请在 Passport 上移动选项并按 OK 确定。");
  } catch (error) {
    pendingRequestId = null;
    setStatus("error", "请求发送失败", error.message);
    throw error;
  } finally {
    sending = false;
    updateControls();
  }
}

async function sendCancel() {
  if (!isConnected() || !pendingRequestId) throw new Error("没有可取消的请求");
  sending = true;
  updateControls();
  try {
    const cancel = buildCancelRequest(pendingRequestId);
    await sendPayload(cancel.payload, "发送 cancel");
    setStatus("success", "取消请求已发送", "设备收到后会回到等待状态。");
    pendingRequestId = null;
  } catch (error) {
    setStatus("error", "取消请求发送失败", error.message);
    throw error;
  } finally {
    sending = false;
    updateControls();
  }
}

function responseDescription(response) {
  if (response.status === "selected") return "用户选择了：" + response.option;
  if (response.status === "cancelled") return "用户取消了这次授权";
  if (response.status === "busy") return "设备正在处理另一条请求";
  if (response.status === "invalid") return "设备认为请求格式无效";
  if (response.status === "conflict") return "设备发现相同请求 ID 的内容不一致";
  return "收到设备响应";
}

function onLinkNotification(event) {
  try {
    const value = event.target.value;
    const bytes = new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
    const frame = decodeLinkMessage(bytes);
    const addresses = readAddressFields();
    if (frame.type !== LINK_MESSAGE_TYPE ||
        frame.sourceId !== addresses.targetId ||
        frame.targetId !== addresses.sourceId ||
        frame.serviceId !== addresses.serviceId) {
      addLog("忽略帧", "地址或 service 不匹配");
      return;
    }
    const raw = textDecoder.decode(frame.payload);
    const response = parseAuthorizationResponse(raw);
    const responseKind = response.status === "selected" || response.status === "cancelled"
      ? "success"
      : response.status === "busy" || response.status === "invalid" || response.status === "conflict"
        ? "error"
        : "idle";
    setResponse(responseKind, responseDescription(response), raw);
    addLog("收到 response", raw);
    if (pendingRequestId === response.rid) {
      pendingRequestId = null;
      updateControls();
    }
    setStatus(
      responseKind === "success" ? "success" : responseKind === "error" ? "error" : "idle",
      "收到设备响应",
      responseDescription(response)
    );
  } catch (error) {
    addLog("无效回传", error.message);
    setResponse("error", "无法解析设备回传", error.message);
    setStatus("error", "设备回传无效", error.message);
  }
}

async function copyPayload() {
  if (!currentRequest) return;
  try {
    if (!navigator.clipboard) throw new Error("当前浏览器不允许访问剪贴板");
    await navigator.clipboard.writeText(currentRequest.json);
    setStatus("success", "JSON 已复制", "可以把这条 payload 交给其他 Agent 客户端测试。");
  } catch (error) {
    setStatus("error", "复制失败", error.message);
  }
}

for (const field of [
  deviceCodeInput,
  sourceCodeInput,
  serviceIdInput,
]) {
  field.addEventListener("input", () => {
    if (isConnected()) disconnectCurrent();
    refreshPreview();
  });
}

for (const field of [
  requestIdInput,
  requestTitleInput,
  requestMessageInput,
  ...optionInputs().flatMap((option) => [option.enabled, option.id, option.label]),
]) {
  field.addEventListener("input", refreshPreview);
  field.addEventListener("change", refreshPreview);
}

requestForm.addEventListener("submit", async (event) => {
  event.preventDefault();
  try {
    await sendRequest();
  } catch {
    updateControls();
  }
});

connectButton.addEventListener("click", async () => {
  try {
    await connectDevice();
  } catch (error) {
    clearConnection(false);
    setConnectionState("error", "连接失败", error.message);
    setStatus("error", "无法连接设备", error.message);
  } finally {
    updateControls();
  }
});

cancelButton.addEventListener("click", async () => {
  try {
    await sendCancel();
  } catch {
    updateControls();
  }
});

copyButton.addEventListener("click", copyPayload);

clearLogButton.addEventListener("click", () => {
  eventLog.replaceChildren();
  addLog("日志", "已清空");
});

if (!window.isSecureContext) {
  setConnectionState("error", "不可用", "当前页面不是安全连接。");
  setStatus("error", "当前页面不是安全连接", "请通过 HTTPS 或 localhost 重新打开本页。");
} else if (!navigator.bluetooth) {
  setConnectionState("error", "不支持", "当前浏览器没有 Web Bluetooth。");
  setStatus("error", "浏览器不支持 Web Bluetooth", "请使用桌面版 Chrome 或 Edge 打开本页。");
}
refreshPreview();

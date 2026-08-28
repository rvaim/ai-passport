import {
  DEVICE_CODE_UUID,
  MAX_PACKAGE_SIZE,
  PACKAGE_CHUNK_SIZE,
  PACKAGE_CONTROL_UUID,
  PACKAGE_DATA_UUID,
  PACKAGE_STATUS_UUID,
  PASSPORT_SERVICE_UUID,
  advertisedNameForDeviceCode,
  crc32,
  formatBytes,
  formatDeviceCode,
  makeBeginControl,
  makeDeviceRequestOptions,
  normalizeDeviceCodeInput,
  parseDeviceCode,
} from "./passport-install-protocol.mjs";
import {
  LINK_MESSAGE_TYPE,
  LINK_RX_UUID,
  LINK_TX_UUID,
  decodeLinkMessage,
  encodeLinkMessage,
} from "./passport-link-protocol.mjs";
import {
  TOTP_SERVICE_ID,
  buildTimeSync,
  buildTotpAccount,
  parseOtpAuthUri,
  parseTotpResponse,
} from "./passport-totp-protocol.mjs";

const pairingInput = document.querySelector("#pairing-code");
const deviceDetail = document.querySelector("#device-detail");
const connectButton = document.querySelector("#connect-button");
const connectionStatus = document.querySelector("#connection-status");
const connectionStatusTitle = document.querySelector("#connection-status-title");
const connectionStatusDetail = document.querySelector("#connection-status-detail");

const fileInput = document.querySelector("#package-file");
const fileButton = document.querySelector("#file-button");
const fileTask = document.querySelector("#file-task");
const fileDetail = document.querySelector("#file-detail");
const installButton = document.querySelector("#install-button");
const progress = document.querySelector("#progress");
const progressLabel = document.querySelector("#progress-label");
const progressValue = document.querySelector("#progress-value");
const installStatus = document.querySelector("#install-status");
const installStatusTitle = document.querySelector("#install-status-title");
const installStatusDetail = document.querySelector("#install-status-detail");

const totpForm = document.querySelector("#totp-form");
const otpAuthUriInput = document.querySelector("#otpauth-uri");
const parseUriButton = document.querySelector("#parse-uri-button");
const issuerInput = document.querySelector("#totp-issuer");
const accountInput = document.querySelector("#totp-account");
const secretInput = document.querySelector("#totp-secret");
const revealSecretButton = document.querySelector("#reveal-secret-button");
const digitsSelect = document.querySelector("#totp-digits");
const periodSelect = document.querySelector("#totp-period");
const syncTimeButton = document.querySelector("#sync-time-button");
const sendTotpButton = document.querySelector("#send-totp-button");
const totpStatus = document.querySelector("#totp-status");
const totpStatusTitle = document.querySelector("#totp-status-title");
const totpStatusDetail = document.querySelector("#totp-status-detail");

const textDecoder = new TextDecoder("utf-8");
const failureStatuses = new Set(["设备码不匹配", "无法写入存储", "写入失败", "安装失败"]);
const bluetoothAvailable = window.isSecureContext && Boolean(navigator.bluetooth);
const statusWaiters = new Set();
const linkWaiters = new Set();

let selectedFile = null;
let selectedDevice = null;
let targetDeviceId = null;
let packageControlCharacteristic = null;
let packageDataCharacteristic = null;
let packageStatusCharacteristic = null;
let linkRxCharacteristic = null;
let linkTxCharacteristic = null;
let connecting = false;
let installing = false;
let totpBusy = false;
let installComplete = false;
let statusSequence = 0;
let latestDeviceStatus = "";
let linkSequence = 0;
let permittedDevices = [];

function randomPeerId() {
  const bytes = globalThis.crypto.getRandomValues(new Uint8Array(6));
  let value = 0n;
  for (let index = bytes.length - 1; index >= 0; index -= 1) {
    value = (value << 8n) | BigInt(bytes[index]);
  }
  return value === 0n ? 1n : value;
}

let browserPeerId = randomPeerId();
let totpRequestCounter = Number(browserPeerId % 46656n);

if (bluetoothAvailable && typeof navigator.bluetooth.getDevices === "function") {
  navigator.bluetooth.getDevices()
    .then((devices) => { permittedDevices = devices; })
    .catch(() => { permittedDevices = []; });
}

function setStatus(box, titleElement, detailElement, kind, title, detail) {
  box.dataset.kind = kind;
  titleElement.textContent = title;
  detailElement.textContent = detail;
}

function setConnectionStatus(kind, title, detail) {
  setStatus(
    connectionStatus,
    connectionStatusTitle,
    connectionStatusDetail,
    kind,
    title,
    detail
  );
}

function setInstallStatus(kind, title, detail) {
  setStatus(
    installStatus,
    installStatusTitle,
    installStatusDetail,
    kind,
    title,
    detail
  );
}

function setTotpStatus(kind, title, detail) {
  setStatus(totpStatus, totpStatusTitle, totpStatusDetail, kind, title, detail);
}

function setProgress(value, label) {
  const bounded = Math.max(0, Math.min(1, value));
  const percentage = Math.round(bounded * 100) + "%";
  progress.value = bounded;
  progress.textContent = percentage;
  progressValue.textContent = percentage;
  progressLabel.textContent = label;
}

function connected() {
  return Boolean(
    selectedDevice?.gatt?.connected &&
    targetDeviceId !== null &&
    packageControlCharacteristic &&
    packageDataCharacteristic &&
    packageStatusCharacteristic &&
    linkRxCharacteristic &&
    linkTxCharacteristic
  );
}

function pairingCodeValid() {
  try {
    parseDeviceCode(pairingInput.value);
    return true;
  } catch {
    return false;
  }
}

function updateControls() {
  const busy = connecting || installing || totpBusy;
  const isConnected = connected();
  const hasAccount = Boolean(accountInput.value.trim() && secretInput.value.trim());

  pairingInput.disabled = busy;
  connectButton.disabled = busy || !bluetoothAvailable || !pairingCodeValid();
  connectButton.textContent = isConnected ? "重新连接" : connecting ? "连接中…" : "连接设备";

  fileInput.disabled = busy;
  fileButton.disabled = busy;
  installButton.disabled = busy || !isConnected || !selectedFile;

  for (const field of [
    otpAuthUriInput,
    issuerInput,
    accountInput,
    secretInput,
    digitsSelect,
    periodSelect,
  ]) {
    field.disabled = busy;
  }
  parseUriButton.disabled = busy;
  revealSecretButton.disabled = busy;
  syncTimeButton.disabled = busy || !isConnected;
  sendTotpButton.disabled = busy || !isConnected || !hasAccount;
}

function validatePackage(file) {
  if (!file) throw new Error("没有选择文件");
  if (!file.name.toLowerCase().endsWith(".pap")) throw new Error("请选择 .pap 安装包");
  if (file.size === 0) throw new Error("安装包是空文件，请重新导出");
  if (file.size > MAX_PACKAGE_SIZE) throw new Error("安装包超过 4 MB，无法写入设备");
  return file;
}

function selectPackage(file) {
  installComplete = false;
  try {
    selectedFile = validatePackage(file);
    const name = document.createElement("span");
    name.className = "file-name";
    name.textContent = selectedFile.name;
    fileDetail.replaceChildren(name, " · " + formatBytes(selectedFile.size));
    fileButton.textContent = "更换文件";
    setProgress(0, connected() ? "可以开始安装" : "连接设备后可以安装");
    setInstallStatus(
      "idle",
      "安装包已选择",
      connected() ? "点击“安装到设备”开始传输。" : "先使用页面顶部连接 Passport。"
    );
  } catch (error) {
    selectedFile = null;
    fileInput.value = "";
    fileDetail.textContent = error.message;
    fileButton.textContent = "重新选择";
    setProgress(0, "等待有效安装包");
    setInstallStatus("error", "无法使用这个文件", error.message);
  }
  updateControls();
}

function settleStatusWaiters(error) {
  for (const waiter of statusWaiters) {
    clearTimeout(waiter.timeoutId);
    waiter.reject(error);
  }
  statusWaiters.clear();
}

function settleLinkWaiters(error) {
  for (const waiter of linkWaiters) {
    clearTimeout(waiter.timeoutId);
    waiter.reject(error);
  }
  linkWaiters.clear();
}

function publishDeviceStatus(text) {
  latestDeviceStatus = text;
  statusSequence += 1;
  for (const waiter of [...statusWaiters]) {
    if (statusSequence <= waiter.afterSequence) continue;
    if (failureStatuses.has(text)) {
      clearTimeout(waiter.timeoutId);
      statusWaiters.delete(waiter);
      waiter.reject(new Error("设备返回：" + text));
    } else if (waiter.predicate(text)) {
      clearTimeout(waiter.timeoutId);
      statusWaiters.delete(waiter);
      waiter.resolve(text);
    }
  }
}

function waitForDeviceStatus(predicate, afterSequence, timeoutMs, timeoutMessage) {
  return new Promise((resolve, reject) => {
    if (statusSequence > afterSequence) {
      if (failureStatuses.has(latestDeviceStatus)) {
        reject(new Error("设备返回：" + latestDeviceStatus));
        return;
      }
      if (predicate(latestDeviceStatus)) {
        resolve(latestDeviceStatus);
        return;
      }
    }
    const waiter = {predicate, afterSequence, resolve, reject, timeoutId: 0};
    waiter.timeoutId = window.setTimeout(() => {
      statusWaiters.delete(waiter);
      reject(new Error(timeoutMessage));
    }, timeoutMs);
    statusWaiters.add(waiter);
  });
}

function onPackageStatusNotification(event) {
  const text = textDecoder.decode(event.target.value).trim();
  if (!text) return;
  publishDeviceStatus(text);
  if (!installing) {
    const failed = failureStatuses.has(text);
    setInstallStatus(
      failed ? "error" : text === "安装成功" ? "success" : "working",
      failed ? "设备未能安装" : text === "安装成功" ? "安装完成" : "设备正在处理",
      text
    );
  }
}

function totpErrorMessage(code) {
  const messages = {
    not_ready: "请先在设备打开“2FA 验证器 → 接收 2FA 密钥”。",
    busy: "插件正在处理上一条请求，请稍后重试。",
    full: "插件已保存 12 个账号，无法继续添加。",
    storage: "插件无法写入私有存储，请在设备上重试。",
    time: "设备拒绝了当前时间，请检查浏览器系统时间。",
    invalid: "插件拒绝了账号数据，请检查输入。",
    encode: "插件无法保存账号数据。",
  };
  return messages[code] || "插件返回未知错误：" + code;
}

function onLinkNotification(event) {
  try {
    const value = event.target.value;
    const bytes = new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
    const frame = decodeLinkMessage(bytes);
    if (frame.type !== LINK_MESSAGE_TYPE ||
        frame.sourceId !== targetDeviceId ||
        frame.targetId !== browserPeerId ||
        frame.serviceId !== TOTP_SERVICE_ID) {
      return;
    }
    const response = parseTotpResponse(textDecoder.decode(frame.payload));
    for (const waiter of [...linkWaiters]) {
      if (response.q !== waiter.requestTag) continue;
      if (response.k === "error") {
        clearTimeout(waiter.timeoutId);
        linkWaiters.delete(waiter);
        waiter.reject(new Error(totpErrorMessage(response.e)));
      } else if (response.k === waiter.expectedKind) {
        clearTimeout(waiter.timeoutId);
        linkWaiters.delete(waiter);
        waiter.resolve(response);
      }
    }
  } catch (error) {
    settleLinkWaiters(new Error("设备回传无效：" + error.message));
  }
}

function clearConnection(announce = true) {
  if (packageStatusCharacteristic) {
    packageStatusCharacteristic.removeEventListener(
      "characteristicvaluechanged",
      onPackageStatusNotification
    );
  }
  if (linkTxCharacteristic) {
    linkTxCharacteristic.removeEventListener(
      "characteristicvaluechanged",
      onLinkNotification
    );
  }
  if (selectedDevice) {
    selectedDevice.removeEventListener("gattserverdisconnected", onDisconnected);
  }
  packageControlCharacteristic = null;
  packageDataCharacteristic = null;
  packageStatusCharacteristic = null;
  linkRxCharacteristic = null;
  linkTxCharacteristic = null;
  targetDeviceId = null;
  selectedDevice = null;
  settleStatusWaiters(new Error("设备连接已断开"));
  settleLinkWaiters(new Error("设备连接已断开"));
  if (announce) {
    setConnectionStatus("error", "设备连接已断开", "请重新连接 Passport。");
    setInstallStatus("error", "设备已断开", "重新连接后可以继续安装。");
    setTotpStatus("error", "设备已断开", "重新连接后可以继续同步或发送。");
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
  connecting = false;
  installing = false;
  totpBusy = false;
  clearConnection(true);
}

async function connectDevice() {
  if (!window.isSecureContext) throw new Error("请通过 HTTPS 或 localhost 打开本页");
  if (!navigator.bluetooth) {
    throw new Error("当前浏览器不支持 Web Bluetooth，请使用桌面版 Chrome 或 Edge");
  }
  const enteredDeviceId = parseDeviceCode(pairingInput.value);
  const pairingCode = formatDeviceCode(enteredDeviceId);
  const advertisedName = advertisedNameForDeviceCode(pairingCode);
  pairingInput.value = pairingCode;
  pairingInput.setAttribute("aria-invalid", "false");
  disconnectCurrent();

  connecting = true;
  setConnectionStatus(
    "working",
    "正在查找设备",
    "请在浏览器窗口中选择 " + advertisedName + "。"
  );
  updateControls();
  let candidate = permittedDevices.find((device) => device.name === advertisedName);
  if (!candidate) {
    candidate = await navigator.bluetooth.requestDevice(makeDeviceRequestOptions());
    if (!permittedDevices.some((device) => device.id === candidate.id)) {
      permittedDevices = [...permittedDevices, candidate];
    }
  }

  try {
    setConnectionStatus(
      "working",
      "正在连接设备",
      candidate.name || "正在建立蓝牙连接…"
    );
    const server = await candidate.gatt.connect();
    const service = await server.getPrimaryService(PASSPORT_SERVICE_UUID);
    const [
      codeCharacteristic,
      nextPackageControl,
      nextPackageData,
      nextPackageStatus,
      nextLinkRx,
      nextLinkTx,
    ] = await Promise.all([
      service.getCharacteristic(DEVICE_CODE_UUID),
      service.getCharacteristic(PACKAGE_CONTROL_UUID),
      service.getCharacteristic(PACKAGE_DATA_UUID),
      service.getCharacteristic(PACKAGE_STATUS_UUID),
      service.getCharacteristic(LINK_RX_UUID),
      service.getCharacteristic(LINK_TX_UUID),
    ]);
    const code = textDecoder.decode(await codeCharacteristic.readValue()).trim().toUpperCase();
    const deviceId = parseDeviceCode(code);
    if (deviceId !== enteredDeviceId) {
      throw new Error("设备码不一致（设备返回 " + code + "）");
    }
    await Promise.all([
      nextPackageStatus.startNotifications(),
      nextLinkTx.startNotifications(),
    ]);
    nextPackageStatus.addEventListener(
      "characteristicvaluechanged",
      onPackageStatusNotification
    );
    nextLinkTx.addEventListener("characteristicvaluechanged", onLinkNotification);

    selectedDevice = candidate;
    targetDeviceId = enteredDeviceId;
    packageControlCharacteristic = nextPackageControl;
    packageDataCharacteristic = nextPackageData;
    packageStatusCharacteristic = nextPackageStatus;
    linkRxCharacteristic = nextLinkRx;
    linkTxCharacteristic = nextLinkTx;
    if (browserPeerId === targetDeviceId) browserPeerId = randomPeerId();
    selectedDevice.addEventListener("gattserverdisconnected", onDisconnected);

    const codeLabel = document.createElement("span");
    codeLabel.className = "device-code";
    codeLabel.textContent = code;
    deviceDetail.replaceChildren("已连接 ", codeLabel, "；设备码复核通过。");
    setConnectionStatus(
      "success",
      "设备已连接",
      "同一连接可用于 PAP 安装和 2FA 密钥发送。"
    );
    if (selectedFile) {
      setProgress(0, "可以开始安装");
      setInstallStatus("idle", "安装已就绪", "点击“安装到设备”开始传输。");
    }
    setTotpStatus(
      "idle",
      "2FA 工具已就绪",
      "在设备打开 2FA 验证器后同步时间或发送账号。"
    );
  } catch (error) {
    if (candidate?.gatt?.connected) candidate.gatt.disconnect();
    throw error;
  } finally {
    connecting = false;
    updateControls();
  }
}

const delay = (milliseconds) =>
  new Promise((resolve) => window.setTimeout(resolve, milliseconds));

async function writeWithRetry(characteristic, value, label) {
  let lastError;
  for (let attempt = 0; attempt < 7; attempt += 1) {
    if (!selectedDevice?.gatt?.connected) throw new Error("设备连接已断开");
    try {
      if (typeof characteristic.writeValueWithResponse === "function") {
        await characteristic.writeValueWithResponse(value);
      } else if (typeof characteristic.writeValueWithoutResponse === "function") {
        await characteristic.writeValueWithoutResponse(value);
      } else {
        await characteristic.writeValue(value);
      }
      return;
    } catch (error) {
      lastError = error;
      if (attempt < 6) await delay(45 + attempt * 25);
    }
  }
  throw new Error(label + "失败：" + (lastError?.message || "蓝牙繁忙"));
}

async function installPackage() {
  const file = validatePackage(selectedFile);
  if (!connected()) throw new Error("请先连接 Passport");

  installing = true;
  installComplete = false;
  updateControls();
  setInstallStatus("working", "正在读取安装包", "校验文件后会开始蓝牙传输。");
  setProgress(0, "正在校验文件");

  const bytes = new Uint8Array(await file.arrayBuffer());
  const checksum = crc32(bytes);
  latestDeviceStatus = "";
  const beginSequence = statusSequence;
  await writeWithRetry(
    packageControlCharacteristic,
    makeBeginControl(bytes.length, checksum, targetDeviceId),
    "启动安装"
  );
  await waitForDeviceStatus(
    (text) => text === "开始接收",
    beginSequence,
    6000,
    "设备没有确认接收，请重试"
  );

  setInstallStatus("working", "正在发送安装包", "请保持设备开机并留在附近。");
  let lastProgressBytes = 0;
  for (let offset = 0; offset < bytes.length; offset += PACKAGE_CHUNK_SIZE) {
    const end = Math.min(offset + PACKAGE_CHUNK_SIZE, bytes.length);
    await writeWithRetry(
      packageDataCharacteristic,
      bytes.subarray(offset, end),
      "发送安装包"
    );
    if (end === bytes.length || end - lastProgressBytes >= 32 * 1024) {
      setProgress(
        end / bytes.length,
        formatBytes(end) + " / " + formatBytes(bytes.length)
      );
      lastProgressBytes = end;
    }
  }

  setInstallStatus("working", "设备正在安装", "正在校验并写入安装包。");
  setProgress(1, "传输完成，等待设备确认");
  const finalSequence = statusSequence;
  await writeWithRetry(
    packageControlCharacteristic,
    new Uint8Array([2]),
    "结束传输"
  );
  await waitForDeviceStatus(
    (text) => text === "安装成功",
    finalSequence,
    120000,
    "等待设备安装结果超时"
  );
  installComplete = true;
  setInstallStatus(
    "success",
    "安装完成",
    "现在可以在 Passport 上使用所安装的插件或主题。"
  );
  setProgress(1, "安装完成");
}

function nextLinkSequence() {
  linkSequence = linkSequence >= 0xffffffff ? 0 : linkSequence + 1;
  return linkSequence;
}

function nextTotpRequestTag() {
  totpRequestCounter = (totpRequestCounter + 1) % 46656;
  return totpRequestCounter.toString(36).padStart(3, "0");
}

function requestTotp(payload, expectedKind, requestTag) {
  if (!connected()) return Promise.reject(new Error("请先连接 Passport"));
  const frame = encodeLinkMessage({
    sourceId: browserPeerId,
    targetId: targetDeviceId,
    serviceId: TOTP_SERVICE_ID,
    sequence: nextLinkSequence(),
    payload,
  });
  return new Promise((resolve, reject) => {
    const waiter = {expectedKind, requestTag, resolve, reject, timeoutId: 0};
    waiter.timeoutId = window.setTimeout(() => {
      linkWaiters.delete(waiter);
      reject(new Error("设备没有回复；请确认 2FA 验证器停留在对应页面"));
    }, 8000);
    linkWaiters.add(waiter);
    writeWithRetry(linkRxCharacteristic, frame, "发送 2FA 数据").catch((error) => {
      clearTimeout(waiter.timeoutId);
      linkWaiters.delete(waiter);
      reject(error);
    });
  });
}

function readTotpForm(requestTag) {
  return buildTotpAccount({
    issuer: issuerInput.value,
    account: accountInput.value,
    secret: secretInput.value,
    digits: Number(digitsSelect.value),
    period: Number(periodSelect.value),
    requestTag,
  });
}

function applyOtpAuthUri() {
  const parsed = parseOtpAuthUri(otpAuthUriInput.value);
  issuerInput.value = parsed.issuer;
  accountInput.value = parsed.account;
  secretInput.value = parsed.secret;
  digitsSelect.value = String(parsed.digits);
  periodSelect.value = String(parsed.period);
  setTotpStatus(
    "success",
    "URI 已读取",
    "请核对账号信息，再发送到设备。"
  );
  updateControls();
}

async function syncTime() {
  totpBusy = true;
  updateControls();
  setTotpStatus(
    "working",
    "正在同步时间",
    "请保持 2FA 验证器在设备前台。"
  );
  try {
    const requestTag = nextTotpRequestTag();
    const request = buildTimeSync({requestTag});
    await requestTotp(request.payload, "time", requestTag);
    setTotpStatus(
      "success",
      "时间同步完成",
      "设备在本次供电期间可以生成 TOTP 验证码。"
    );
  } finally {
    totpBusy = false;
    updateControls();
  }
}

async function sendTotpAccount() {
  const requestTag = nextTotpRequestTag();
  const request = readTotpForm(requestTag);
  totpBusy = true;
  updateControls();
  setTotpStatus(
    "working",
    "正在发送账号",
    "等待插件保存 " + (request.issuer || request.account) + "。"
  );
  try {
    await requestTotp(request.payload, "added", requestTag);
    secretInput.value = "";
    otpAuthUriInput.value = "";
    secretInput.type = "password";
    revealSecretButton.textContent = "显示密钥";
    revealSecretButton.setAttribute("aria-pressed", "false");
    setTotpStatus(
      "success",
      "账号已保存",
      "插件已确认写入私有持久化存储。"
    );
  } finally {
    totpBusy = false;
    updateControls();
  }
}

fileButton.addEventListener("click", () => fileInput.click());
fileInput.addEventListener("change", () => selectPackage(fileInput.files?.[0]));

for (const eventName of ["dragenter", "dragover"]) {
  fileTask.addEventListener(eventName, (event) => {
    event.preventDefault();
    if (!connecting && !installing && !totpBusy) {
      fileTask.classList.add("is-dragging");
    }
  });
}

for (const eventName of ["dragleave", "drop"]) {
  fileTask.addEventListener(eventName, (event) => {
    event.preventDefault();
    fileTask.classList.remove("is-dragging");
  });
}

fileTask.addEventListener("drop", (event) => {
  if (!connecting && !installing && !totpBusy) {
    selectPackage(event.dataTransfer?.files?.[0]);
  }
});

pairingInput.addEventListener("input", () => {
  const before = pairingInput.value;
  const caret = pairingInput.selectionStart ?? before.length;
  const compactCaret = before.slice(0, caret).replace(/[-\s]/g, "").length;
  pairingInput.value = normalizeDeviceCodeInput(before);
  const formattedCaret = Math.min(
    pairingInput.value.length,
    compactCaret + (compactCaret > 5 ? 1 : 0) + (compactCaret > 10 ? 1 : 0)
  );
  pairingInput.setSelectionRange(formattedCaret, formattedCaret);
  pairingInput.setAttribute("aria-invalid", "false");
  if (connected()) disconnectCurrent();

  try {
    const advertisedName = advertisedNameForDeviceCode(pairingInput.value);
    pairingInput.value = formatDeviceCode(parseDeviceCode(pairingInput.value));
    deviceDetail.textContent =
      "浏览器将显示附近的 Passport；请选择 " + advertisedName + "。";
    setConnectionStatus("idle", "可以连接", "设备码格式和校验位有效。");
  } catch (error) {
    const compactLength = pairingInput.value.replace(/[-\s]/g, "").length;
    const completeButInvalid = compactLength >= 11;
    pairingInput.setAttribute("aria-invalid", completeButInvalid ? "true" : "false");
    deviceDetail.textContent = !pairingInput.value.trim()
      ? "设备码可在“设置 → 设备信息”或“插件管理”页查看。"
      : completeButInvalid
        ? error.message
        : "继续输入完整设备码，格式为 XXXXX-XXXXX-X。";
    setConnectionStatus(
      completeButInvalid ? "error" : "idle",
      completeButInvalid ? "设备码无效" : "尚未连接",
      completeButInvalid ? error.message : "输入完整设备码后连接 Passport。"
    );
  }
  updateControls();
});

pairingInput.addEventListener("blur", () => {
  if (!pairingInput.value.trim()) return;
  try {
    pairingInput.value = formatDeviceCode(parseDeviceCode(pairingInput.value));
    pairingInput.setAttribute("aria-invalid", "false");
  } catch (error) {
    pairingInput.setAttribute("aria-invalid", "true");
    deviceDetail.textContent = error.message;
  }
  updateControls();
});

for (const field of [accountInput, secretInput]) {
  field.addEventListener("input", updateControls);
}

connectButton.addEventListener("click", async () => {
  try {
    await connectDevice();
  } catch (error) {
    clearConnection(false);
    setConnectionStatus("error", "无法连接设备", error.message);
  } finally {
    connecting = false;
    updateControls();
  }
});

installButton.addEventListener("click", async () => {
  try {
    await installPackage();
  } catch (error) {
    setInstallStatus(
      "error",
      "安装失败",
      error.message + "。请确认设备仍在附近后重试。"
    );
  } finally {
    installing = false;
    if (!installComplete && selectedFile) {
      setProgress(progress.value, progressLabel.textContent);
    }
    updateControls();
  }
});

parseUriButton.addEventListener("click", () => {
  try {
    applyOtpAuthUri();
  } catch (error) {
    setTotpStatus("error", "无法读取 URI", error.message);
  }
});

revealSecretButton.addEventListener("click", () => {
  const reveal = secretInput.type === "password";
  secretInput.type = reveal ? "text" : "password";
  revealSecretButton.textContent = reveal ? "隐藏密钥" : "显示密钥";
  revealSecretButton.setAttribute("aria-pressed", reveal ? "true" : "false");
  secretInput.focus();
});

syncTimeButton.addEventListener("click", async () => {
  try {
    await syncTime();
  } catch (error) {
    setTotpStatus("error", "时间同步失败", error.message);
  }
});

totpForm.addEventListener("submit", async (event) => {
  event.preventDefault();
  try {
    await sendTotpAccount();
  } catch (error) {
    setTotpStatus("error", "密钥发送失败", error.message);
  }
});

if (!window.isSecureContext) {
  setConnectionStatus(
    "error",
    "当前页面不是安全连接",
    "请通过 HTTPS 或 localhost 重新打开本页。"
  );
} else if (!navigator.bluetooth) {
  setConnectionStatus(
    "error",
    "浏览器不支持 Web Bluetooth",
    "请改用桌面版 Chrome 或 Edge 打开本页。"
  );
} else {
  updateControls();
}

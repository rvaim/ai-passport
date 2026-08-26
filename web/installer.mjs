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
  parseDeviceCode,
} from "./passport-install-protocol.mjs";

const fileInput = document.querySelector("#package-file");
const fileButton = document.querySelector("#file-button");
const fileTask = document.querySelector("#file-task");
const fileDetail = document.querySelector("#file-detail");
const pairingInput = document.querySelector("#pairing-code");
const deviceDetail = document.querySelector("#device-detail");
const connectButton = document.querySelector("#connect-button");
const installButton = document.querySelector("#install-button");
const progress = document.querySelector("#progress");
const progressLabel = document.querySelector("#progress-label");
const progressValue = document.querySelector("#progress-value");
const statusBox = document.querySelector("#status");
const statusTitle = document.querySelector("#status-title");
const statusDetail = document.querySelector("#status-detail");

const textDecoder = new TextDecoder("utf-8");
const failureStatuses = new Set(["设备码不匹配", "无法写入存储", "写入失败", "安装失败"]);
const bluetoothAvailable = window.isSecureContext && Boolean(navigator.bluetooth);

let selectedFile = null;
let selectedDevice = null;
let targetDeviceId = null;
let controlCharacteristic = null;
let dataCharacteristic = null;
let statusCharacteristic = null;
let installing = false;
let statusSequence = 0;
let latestDeviceStatus = "";
let permittedDevices = [];
const statusWaiters = new Set();

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

function setProgress(value, label) {
  const bounded = Math.max(0, Math.min(1, value));
  progress.value = bounded;
  progress.textContent = `${Math.round(bounded * 100)}%`;
  progressValue.textContent = `${Math.round(bounded * 100)}%`;
  progressLabel.textContent = label;
}

function updateControls() {
  const connected = Boolean(selectedDevice?.gatt?.connected && targetDeviceId !== null);
  let validPairingCode = false;
  try {
    parseDeviceCode(pairingInput.value);
    validPairingCode = true;
  } catch {
    validPairingCode = false;
  }
  fileInput.disabled = installing;
  fileButton.disabled = installing;
  pairingInput.disabled = installing;
  connectButton.disabled = installing || !bluetoothAvailable || !validPairingCode;
  installButton.disabled = installing || !selectedFile || !connected;
  connectButton.textContent = connected ? "重新连接" : "连接设备";
  if (!installing && selectedFile && connected) {
    setProgress(0, "可以开始安装");
  } else if (!installing && !selectedFile && !connected) {
    setProgress(0, "等待文件和设备");
  } else if (!installing && !selectedFile) {
    setProgress(0, "等待插件包");
  } else if (!installing && !connected) {
    setProgress(0, "等待设备连接");
  }
}

function validatePackage(file) {
  if (!file) throw new Error("没有选择文件");
  if (!file.name.toLowerCase().endsWith(".pap")) throw new Error("请选择 .pap 插件包");
  if (file.size === 0) throw new Error("插件包是空文件，请重新导出");
  if (file.size > MAX_PACKAGE_SIZE) throw new Error("插件包超过 4 MB，无法写入设备");
  return file;
}

function selectPackage(file) {
  try {
    selectedFile = validatePackage(file);
    const name = document.createElement("span");
    name.className = "file-name";
    name.textContent = selectedFile.name;
    fileDetail.replaceChildren(name, ` · ${formatBytes(selectedFile.size)}`);
    fileButton.textContent = "更换文件";
    setStatus("idle", "插件包已选择", "继续连接要安装到的 Passport。");
  } catch (error) {
    selectedFile = null;
    fileInput.value = "";
    fileDetail.textContent = error.message;
    fileButton.textContent = "重新选择";
    setStatus("error", "无法使用这个文件", error.message);
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

function publishDeviceStatus(text) {
  latestDeviceStatus = text;
  statusSequence += 1;
  for (const waiter of [...statusWaiters]) {
    if (statusSequence <= waiter.afterSequence) continue;
    if (failureStatuses.has(text)) {
      clearTimeout(waiter.timeoutId);
      statusWaiters.delete(waiter);
      waiter.reject(new Error(`设备返回：${text}`));
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
        reject(new Error(`设备返回：${latestDeviceStatus}`));
        return;
      }
      if (predicate(latestDeviceStatus)) {
        resolve(latestDeviceStatus);
        return;
      }
    }

    const waiter = { predicate, afterSequence, resolve, reject, timeoutId: 0 };
    waiter.timeoutId = window.setTimeout(() => {
      statusWaiters.delete(waiter);
      reject(new Error(timeoutMessage));
    }, timeoutMs);
    statusWaiters.add(waiter);
  });
}

function onStatusNotification(event) {
  const text = textDecoder.decode(event.target.value).trim();
  if (!text) return;
  publishDeviceStatus(text);
  if (!installing) {
    const failed = failureStatuses.has(text);
    setStatus(failed ? "error" : text === "安装成功" ? "success" : "working",
      failed ? "设备未能安装" : text === "安装成功" ? "插件安装完成" : "设备正在处理",
      text);
  }
}

function clearConnection(announce = true) {
  if (statusCharacteristic) {
    statusCharacteristic.removeEventListener("characteristicvaluechanged", onStatusNotification);
  }
  controlCharacteristic = null;
  dataCharacteristic = null;
  statusCharacteristic = null;
  targetDeviceId = null;
  if (selectedDevice) selectedDevice.removeEventListener("gattserverdisconnected", onDisconnected);
  selectedDevice = null;
  deviceDetail.textContent = "浏览器先显示附近的 Passport，连接后会用配对码核对目标设备。";
  settleStatusWaiters(new Error("设备连接已断开"));
  if (announce) setStatus("error", "设备连接已断开", "请重新选择 Passport 后再安装。");
  updateControls();
}

function onDisconnected() {
  installing = false;
  clearConnection(true);
}

async function connectDevice() {
  if (!window.isSecureContext) throw new Error("请通过 HTTPS 或 localhost 打开安装页");
  if (!navigator.bluetooth) throw new Error("当前浏览器不支持 Web Bluetooth，请使用桌面版 Chrome 或 Edge");
  const enteredDeviceId = parseDeviceCode(pairingInput.value);
  const pairingCode = formatDeviceCode(enteredDeviceId);
  const advertisedName = advertisedNameForDeviceCode(pairingCode);
  pairingInput.value = pairingCode;
  pairingInput.setAttribute("aria-invalid", "false");

  if (selectedDevice?.gatt?.connected) {
    selectedDevice.removeEventListener("gattserverdisconnected", onDisconnected);
    selectedDevice.gatt.disconnect();
    clearConnection(false);
  }

  setStatus("working", "正在查找设备", `请在浏览器窗口中选择 ${advertisedName}。`);
  connectButton.disabled = true;
  let candidate = permittedDevices.find((device) => device.name === advertisedName);
  if (!candidate) {
    candidate = await navigator.bluetooth.requestDevice(makeDeviceRequestOptions());
    if (!permittedDevices.some((device) => device.id === candidate.id)) {
      permittedDevices = [...permittedDevices, candidate];
    }
  }

  try {
    setStatus("working", "正在连接设备", candidate.name || "正在建立蓝牙连接…");
    const server = await candidate.gatt.connect();
    const service = await server.getPrimaryService(PASSPORT_SERVICE_UUID);
    const [codeCharacteristic, nextControl, nextData, nextStatus] = await Promise.all([
      service.getCharacteristic(DEVICE_CODE_UUID),
      service.getCharacteristic(PACKAGE_CONTROL_UUID),
      service.getCharacteristic(PACKAGE_DATA_UUID),
      service.getCharacteristic(PACKAGE_STATUS_UUID),
    ]);
    const code = textDecoder.decode(await codeCharacteristic.readValue()).trim().toUpperCase();
    const deviceId = parseDeviceCode(code);
    if (deviceId !== enteredDeviceId) {
      throw new Error(`配对码与设备返回的设备码不一致（设备返回 ${code}）`);
    }
    await nextStatus.startNotifications();
    nextStatus.addEventListener("characteristicvaluechanged", onStatusNotification);

    selectedDevice = candidate;
    targetDeviceId = enteredDeviceId;
    controlCharacteristic = nextControl;
    dataCharacteristic = nextData;
    statusCharacteristic = nextStatus;
    selectedDevice.addEventListener("gattserverdisconnected", onDisconnected);
    const codeLabel = document.createElement("span");
    codeLabel.className = "device-code";
    codeLabel.textContent = code;
    deviceDetail.replaceChildren("已连接 ", codeLabel, "；设备返回的配对码复核通过。");
    setStatus("success", "设备已连接", `配对码 ${code} 已核对，可以安装插件。`);
  } catch (error) {
    if (candidate.gatt.connected) candidate.gatt.disconnect();
    throw error;
  } finally {
    updateControls();
  }
}

const delay = (milliseconds) => new Promise((resolve) => window.setTimeout(resolve, milliseconds));

async function writeWithRetry(characteristic, value, label) {
  let lastError;
  for (let attempt = 0; attempt < 7; attempt += 1) {
    if (!selectedDevice?.gatt?.connected) throw new Error("设备连接已断开");
    try {
      if (typeof characteristic.writeValueWithResponse === "function") {
        await characteristic.writeValueWithResponse(value);
      } else {
        await characteristic.writeValue(value);
      }
      return;
    } catch (error) {
      lastError = error;
      if (attempt < 6) await delay(45 + attempt * 25);
    }
  }
  throw new Error(`${label}失败：${lastError?.message || "蓝牙繁忙"}`);
}

async function installPackage() {
  const file = validatePackage(selectedFile);
  if (!selectedDevice?.gatt?.connected || targetDeviceId === null ||
      !controlCharacteristic || !dataCharacteristic || !statusCharacteristic) {
    throw new Error("请先连接 Passport");
  }

  installing = true;
  updateControls();
  setStatus("working", "正在读取插件包", "校验文件后会开始蓝牙传输。");
  setProgress(0, "正在校验文件");

  const bytes = new Uint8Array(await file.arrayBuffer());
  const checksum = crc32(bytes);
  latestDeviceStatus = "";
  const beginSequence = statusSequence;
  await writeWithRetry(
    controlCharacteristic,
    makeBeginControl(bytes.length, checksum, targetDeviceId),
    "启动安装"
  );
  await waitForDeviceStatus(
    (text) => text === "开始接收",
    beginSequence,
    6000,
    "设备没有确认接收，请重试"
  );

  setStatus("working", "正在发送插件", "请保持设备开机并留在附近。");
  let lastProgressBytes = 0;
  for (let offset = 0; offset < bytes.length; offset += PACKAGE_CHUNK_SIZE) {
    const end = Math.min(offset + PACKAGE_CHUNK_SIZE, bytes.length);
    await writeWithRetry(dataCharacteristic, bytes.subarray(offset, end), "发送插件");
    if (end === bytes.length || end - lastProgressBytes >= 32 * 1024) {
      const ratio = end / bytes.length;
      setProgress(ratio, `${formatBytes(end)} / ${formatBytes(bytes.length)}`);
      lastProgressBytes = end;
    }
  }

  setStatus("working", "设备正在安装", "传输完成，正在校验并写入插件。");
  setProgress(1, "传输完成，等待设备确认");
  const finalSequence = statusSequence;
  await writeWithRetry(controlCharacteristic, new Uint8Array([2]), "结束传输");
  await waitForDeviceStatus(
    (text) => text === "安装成功",
    finalSequence,
    120000,
    "等待设备安装结果超时"
  );

  setStatus("success", "插件安装完成", "现在可以在设备的插件管理中打开它。");
  setProgress(1, "安装完成");
}

fileButton.addEventListener("click", () => fileInput.click());
fileInput.addEventListener("change", () => selectPackage(fileInput.files?.[0]));

for (const eventName of ["dragenter", "dragover"]) {
  fileTask.addEventListener(eventName, (event) => {
    event.preventDefault();
    if (!installing) fileTask.classList.add("is-dragging");
  });
}

for (const eventName of ["dragleave", "drop"]) {
  fileTask.addEventListener(eventName, (event) => {
    event.preventDefault();
    fileTask.classList.remove("is-dragging");
  });
}

fileTask.addEventListener("drop", (event) => {
  if (!installing) selectPackage(event.dataTransfer?.files?.[0]);
});

pairingInput.addEventListener("input", () => {
  pairingInput.value = pairingInput.value.toUpperCase();
  pairingInput.setAttribute("aria-invalid", "false");
  if (selectedDevice?.gatt?.connected) {
    selectedDevice.removeEventListener("gattserverdisconnected", onDisconnected);
    selectedDevice.gatt.disconnect();
    clearConnection(false);
  }
  try {
    const advertisedName = advertisedNameForDeviceCode(pairingInput.value);
    pairingInput.value = formatDeviceCode(parseDeviceCode(pairingInput.value));
    deviceDetail.textContent = `浏览器将显示附近的 Passport；请选择 ${advertisedName}，连接后会再次核对配对码。`;
  } catch (error) {
    const compactLength = pairingInput.value.replace(/[-\s]/g, "").length;
    const completeButInvalid = compactLength >= 11;
    pairingInput.setAttribute("aria-invalid", completeButInvalid ? "true" : "false");
    deviceDetail.textContent = !pairingInput.value.trim()
      ? "浏览器先显示附近的 Passport，连接后会用配对码核对目标设备。"
      : completeButInvalid
        ? error.message
        : "继续输入完整配对码，格式为 XXXXX-XXXXX-X。";
  }
  updateControls();
});

pairingInput.addEventListener("blur", () => {
  if (!pairingInput.value.trim()) return;
  try {
    pairingInput.value = formatDeviceCode(parseDeviceCode(pairingInput.value));
    pairingInput.setAttribute("aria-invalid", "false");
    deviceDetail.textContent = `浏览器将显示附近的 Passport；请选择 ${advertisedNameForDeviceCode(pairingInput.value)}，连接后会再次核对配对码。`;
  } catch (error) {
    pairingInput.setAttribute("aria-invalid", "true");
    deviceDetail.textContent = error.message;
  }
  updateControls();
});

connectButton.addEventListener("click", async () => {
  try {
    await connectDevice();
  } catch (error) {
    clearConnection(false);
    setStatus("error", "无法连接设备", error.message);
  } finally {
    updateControls();
  }
});

installButton.addEventListener("click", async () => {
  try {
    await installPackage();
  } catch (error) {
    setStatus("error", "插件安装失败", `${error.message}。请确认设备仍在附近后重试。`);
  } finally {
    installing = false;
    updateControls();
  }
});

if (!window.isSecureContext) {
  connectButton.disabled = true;
  setStatus("error", "当前页面不是安全连接", "请通过 HTTPS 或 localhost 重新打开本页。");
} else if (!navigator.bluetooth) {
  connectButton.disabled = true;
  setStatus("error", "浏览器不支持蓝牙安装", "请改用桌面版 Chrome 或 Edge 打开本页。");
} else {
  updateControls();
}

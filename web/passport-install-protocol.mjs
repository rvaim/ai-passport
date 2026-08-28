export const PASSPORT_SERVICE_UUID = "0100004b-4e49-4c54-524f-505353415031";
export const DEVICE_CODE_UUID = "01000045-444f-4354-524f-505353415031";
export const PACKAGE_CONTROL_UUID = "01000043-474b-5054-524f-505353415031";
export const PACKAGE_DATA_UUID = "01000044-474b-5054-524f-505353415031";
export const PACKAGE_STATUS_UUID = "01000053-474b-5054-524f-505353415031";

export const MAX_PACKAGE_SIZE = 4 * 1024 * 1024;
export const PACKAGE_CHUNK_SIZE = 180;

const DEVICE_ID_MAX = 0xffffffffffffn;
const DEVICE_CODE_ALPHABET = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";

export function normalizeDeviceCodeInput(value) {
  const compact = String(value ?? "")
    .toUpperCase()
    .replace(/[-\s]/g, "")
    .slice(0, 11);
  return [compact.slice(0, 5), compact.slice(5, 10), compact.slice(10, 11)]
    .filter(Boolean)
    .join("-");
}

export function formatDeviceCode(deviceId) {
  if (typeof deviceId !== "bigint" || deviceId < 0n || deviceId > DEVICE_ID_MAX) {
    throw new RangeError("设备标识超出 48 位范围");
  }

  const digits = new Array(10);
  let value = deviceId;
  let check = 0;
  for (let index = 9; index >= 0; index -= 1) {
    const digit = Number(value & 31n);
    digits[index] = DEVICE_CODE_ALPHABET[digit];
    check = (check + (index + 1) * digit) % 32;
    value >>= 5n;
  }
  const compact = digits.join("");
  return `${compact.slice(0, 5)}-${compact.slice(5)}-${DEVICE_CODE_ALPHABET[check]}`;
}

export function parseDeviceCode(code) {
  const compact = String(code ?? "")
    .toUpperCase()
    .replace(/[-\s]/g, "");
  if (compact.length !== 11) {
    throw new Error("设备码格式应为 XXXXX-XXXXX-X");
  }

  let value = 0n;
  let check = 0;
  for (let index = 0; index < 10; index += 1) {
    const digit = DEVICE_CODE_ALPHABET.indexOf(compact[index]);
    if (digit < 0) throw new Error("设备码包含无效字符");
    value = (value << 5n) | BigInt(digit);
    check = (check + (index + 1) * digit) % 32;
  }

  if (DEVICE_CODE_ALPHABET.indexOf(compact[10]) !== check || value > DEVICE_ID_MAX) {
    throw new Error("设备码校验失败");
  }
  return value;
}

export function advertisedNameForDeviceCode(code) {
  return `Passport-${formatDeviceCode(parseDeviceCode(code))}`;
}

export function makeDeviceRequestOptions() {
  return {
    filters: [{ services: [PASSPORT_SERVICE_UUID] }],
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

export function makeBeginControl(totalSize, checksum, targetId) {
  if (!Number.isInteger(totalSize) || totalSize <= 0 || totalSize > MAX_PACKAGE_SIZE) {
    throw new RangeError("插件包大小无效");
  }
  if (!Number.isInteger(checksum) || checksum < 0 || checksum > 0xffffffff) {
    throw new RangeError("CRC32 无效");
  }
  if (typeof targetId !== "bigint" || targetId < 0n || targetId > DEVICE_ID_MAX) {
    throw new RangeError("目标设备标识无效");
  }

  const bytes = new Uint8Array(17);
  const view = new DataView(bytes.buffer);
  view.setUint8(0, 1);
  view.setUint32(1, totalSize, true);
  view.setUint32(5, checksum, true);
  let remaining = targetId;
  for (let index = 0; index < 8; index += 1) {
    bytes[9 + index] = Number(remaining & 0xffn);
    remaining >>= 8n;
  }
  return bytes;
}

export function formatBytes(bytes) {
  if (!Number.isFinite(bytes) || bytes < 0) return "0 B";
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
}

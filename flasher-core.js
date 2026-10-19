// Flashing logic, kept free of any page/DOM code so it can be tested on its own.
import { ESPLoader, Transport } from "./lib/esptool/index.js";
import { SerialPort as UsbSerialPort } from "./lib/polyfill/serial.js";
import { md5Hex } from "./md5.js";

export const ESPRESSIF_VID = 0x303a;

// Chip IDs found at byte 12 of an ESP firmware image header.
const IMAGE_CHIP_NAMES = {
  0: "ESP32", 2: "ESP32-S2", 5: "ESP32-C3", 9: "ESP32-S3", 12: "ESP32-C2",
  13: "ESP32-C6", 16: "ESP32-H2", 18: "ESP32-P4", 23: "ESP32-C5",
};

/** Which USB route this browser offers. */
export function transportSupport(nav = globalThis.navigator) {
  if (!globalThis.isSecureContext) return { kind: "none", reason: "insecure" };
  if (nav && nav.serial) return { kind: "webserial" };
  if (nav && nav.usb) return { kind: "webusb" };
  return { kind: "none", reason: "unsupported" };
}

const USB_CLASS_NAMES = {
  0x00: "per-interface", 0x01: "audio", 0x02: "CDC control (serial)", 0x03: "HID", 0x08: "mass storage",
  0x09: "hub", 0x0a: "CDC data (serial)", 0xe0: "wireless", 0xef: "misc", 0xfe: "app-specific", 0xff: "vendor-specific",
};
const hex4 = (n) => (n ?? 0).toString(16).padStart(4, "0");

/** Human-readable summary of a WebUSB device, for the log. */
export function describeUsbDevice(dev) {
  const cfg = dev.configuration ?? dev.configurations?.[0];
  const ifaces = (cfg?.interfaces ?? []).map((i) => {
    const c = i.alternates?.[0]?.interfaceClass;
    return `#${i.interfaceNumber} ${USB_CLASS_NAMES[c] ?? "class 0x" + (c ?? 0).toString(16)}`;
  });
  return `${hex4(dev.vendorId)}:${hex4(dev.productId)} "${dev.manufacturerName ?? "?"} / ${dev.productName ?? "?"}"` +
    ` class 0x${(dev.deviceClass ?? 0).toString(16)}; ${cfg ? "interfaces: " + (ifaces.join(", ") || "none") : "no configuration info"}`;
}

/**
 * Ask the user to pick the board. On Android (no Web Serial) this goes through
 * WebUSB plus Google's CDC-ACM polyfill, which matches the ESP32-S3 built-in USB.
 *
 * Filters by vendor only. The polyfill's own requestPort() also adds an
 * interface-class filter, which some Android Chrome builds fail to match even
 * when the board is attached ("No compatible devices found").
 * @param {{anyDevice?: boolean, log?: (s: string) => void}} opts anyDevice=true lists every USB device.
 */
export async function requestPort({ anyDevice = false, log = () => {} } = {}) {
  const support = transportSupport();
  if (support.kind === "webserial") {
    return navigator.serial.requestPort({ filters: anyDevice ? [] : [{ usbVendorId: ESPRESSIF_VID }] });
  }
  if (support.kind === "webusb") {
    const device = await navigator.usb.requestDevice({ filters: anyDevice ? [] : [{ vendorId: ESPRESSIF_VID }] });
    log("USB device: " + describeUsbDevice(device));
    try {
      return new UsbSerialPort(device);
    } catch (e) {
      throw new Error(`That device (${hex4(device.vendorId)}:${hex4(device.productId)}) has no USB serial interface, so it can't be flashed this way. ` +
        "If it's the ESP32, it isn't in download mode: hold BOOT while plugging it in.");
    }
  }
  throw new Error(support.reason === "insecure"
    ? "This page must be opened over https:// for USB access."
    : "This browser has no USB access. Use Chrome on Android or desktop.");
}

/** Sanity-check a firmware image before it goes anywhere near the chip. */
export function inspectImage(data, address) {
  const info = { ok: true, warnings: [], chipId: null, chipName: null };
  if (!(data instanceof Uint8Array) || data.length === 0) {
    return { ...info, ok: false, error: "The file is empty." };
  }
  if (data[0] !== 0xe9) {
    if (address === 0) return { ...info, ok: false, error: "This doesn't look like an ESP firmware image (bad magic byte)." };
    info.warnings.push("File doesn't start with an ESP image header. Only continue if you know it's raw data (e.g. a filesystem image).");
    return info;
  }
  if (data.length >= 14) {
    info.chipId = data[12] | (data[13] << 8);
    info.chipName = IMAGE_CHIP_NAMES[info.chipId] ?? `chip id ${info.chipId}`;
  }
  const appDescMagic = data.length >= 0x24
    ? (data[0x20] | (data[0x21] << 8) | (data[0x22] << 16) | (data[0x23] << 24)) >>> 0 : 0;
  const isApp = appDescMagic === 0xabcd5432;
  if (address === 0 && isApp) {
    return { ...info, ok: false, error: "This is an app-only image; it belongs at 0x10000, not 0x0. For a first flash use the FULL-FLASH file." };
  }
  if (address !== 0 && !isApp && address < 0x8000) {
    info.warnings.push("This looks like a bootloader/merged image but the address isn't 0x0.");
  }
  return info;
}

/**
 * Connect, upload the flasher stub, write every image, verify MD5, reboot.
 * @param {object} p
 * @param {*} p.port SerialPort (native or polyfilled)
 * @param {{data: Uint8Array, address: number, name?: string}[]} p.images
 * @param {boolean} [p.eraseAll]
 * @param {(line: string) => void} [p.log]
 * @param {(stage: string, fraction: number) => void} [p.progress]
 * @param {object} [p.loaderOverrides] for tests (timeouts, reset strategy)
 */
export async function flashImages({ port, images, eraseAll = false, log = () => {}, progress = () => {}, loaderOverrides = {} }) {
  const terminal = {
    clean() {},
    writeLine: (d) => log(String(d)),
    write: (d) => { const s = String(d).trim(); if (s) log(s); },
  };
  const transport = new Transport(port, false);
  // USB-Serial/JTAG ignores baud rate, so stay at 115200 and skip the risky
  // baud-change port reopen entirely.
  const loader = new ESPLoader({ transport, baudrate: 115200, romBaudrate: 115200, terminal, ...loaderOverrides });

  try {
    progress("connect", 0);
    log("Connecting to chip…");
    const chipDesc = await loader.main(loaderOverrides.resetMode ?? "default_reset");
    log(`Connected: ${chipDesc}`);

    for (const img of images) {
      const chipId = img.data[0] === 0xe9 && img.data.length >= 14 ? img.data[12] | (img.data[13] << 8) : null;
      if (chipId !== null && loader.chip.IMAGE_CHIP_ID !== undefined && chipId !== loader.chip.IMAGE_CHIP_ID) {
        throw new Error(`${img.name ?? "Image"} was built for ${IMAGE_CHIP_NAMES[chipId] ?? "chip id " + chipId}, but the board is ${loader.chip.CHIP_NAME}. Nothing was written.`);
      }
    }

    const total = images.reduce((n, i) => n + i.data.length, 0);
    const doneBefore = images.map((_, k) => images.slice(0, k).reduce((n, i) => n + i.data.length, 0));
    if (eraseAll) { progress("erase", 0); log("Erasing entire flash (can take ~30 s)…"); }
    progress("write", 0);
    await loader.writeFlash({
      fileArray: images.map((i) => ({ data: i.data, address: i.address })),
      flashMode: "keep", flashFreq: "keep", flashSize: "keep",
      eraseAll, compress: true,
      reportProgress: (idx, written, size) => progress("write", (doneBefore[idx] + (written / size) * images[idx].data.length) / total),
      calculateMD5Hash: (image) => md5Hex(image),
    });
    progress("write", 1);
    log("All data verified (MD5 match).");

    progress("reset", 1);
    try {
      await loader.after("hard_reset");
      log("Board reset. It is now booting the new firmware.");
      return { chip: chipDesc, resetOk: true };
    } catch (e) {
      log("Couldn't reset the board automatically: " + (e?.message ?? e));
      return { chip: chipDesc, resetOk: false };
    }
  } finally {
    try { await transport.disconnect(); } catch { /* port may already be gone after reset */ }
  }
}

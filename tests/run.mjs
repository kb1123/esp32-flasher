import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
// Browsers fetch the stub JSON over https; Node's fetch can't read file:// URLs, so serve them from disk.
const realFetch = globalThis.fetch;
globalThis.fetch = async (u, o) => {
  const s = String(u);
  if (s.startsWith("file:")) return new Response(readFileSync(fileURLToPath(s)), { headers: { "content-type": "application/json" } });
  return realFetch(u, o);
};
import { SerialPort } from "../lib/polyfill/serial.js";
import { flashImages, inspectImage, requestPort } from "../flasher-core.js";
import { EspS3Emulator, FakeUsbDevice } from "./sim.mjs";

const fw = new Uint8Array(readFileSync(new URL("../firmware/ota-base-FULL-FLASH-0x0.bin", import.meta.url)));
const app = new Uint8Array(readFileSync(new URL("../builds/ota-base-v1.2.0-OTA.bin", import.meta.url)));
let failures = 0;
const check = (cond, msg) => { console.log((cond ? "  PASS " : "  FAIL ") + msg); if (!cond) failures++; };

async function run(name, { emuOpts = {}, images = [{ name: "fw", data: fw, address: 0 }], eraseAll = false } = {}) {
  console.log(`\n== ${name}`);
  const emu = new EspS3Emulator(emuOpts);
  const dev = new FakeUsbDevice(emu);
  const port = new SerialPort(dev);           // the real polyfill, as Chrome on Android would use it
  const logs = []; const prog = [];
  const t0 = Date.now();
  let result, error;
  try {
    result = await flashImages({ port, images, eraseAll, log: (l) => logs.push(l), progress: (s, f) => prog.push([s, f]) });
  } catch (e) { error = e; }
  return { emu, dev, logs, prog, result, error, ms: Date.now() - t0 };
}

// 1. Happy path: full-flash image at 0x0
{
  const r = await run("full flash at 0x0");
  if (r.error) console.log("  error:", r.error.stack);
  check(!r.error, "completes without error");
  check(r.result?.chip?.includes("ESP32-S3"), `detected chip: ${r.result?.chip}`);
  check(Buffer.compare(Buffer.from(r.emu.flash.subarray(0, fw.length)), Buffer.from(fw)) === 0, `flash contents match file byte-for-byte (${fw.length} bytes)`);
  check(r.emu.flash.subarray(fw.length, fw.length + 4096).every((b) => b === 0xff), "nothing written past the image");
  check(r.logs.some((l) => /verified/i.test(l)), "MD5 verification ran and passed");
  check(r.logs.some((l) => /Stub running/i.test(l)), "flasher stub was uploaded and started");
  const w = r.prog.filter(([s]) => s === "write").map(([, f]) => f);
  check(w.length > 3 && w.every((f, i) => i === 0 || f >= w[i - 1]) && w.at(-1) === 1, `progress monotonic to 100% (${w.length} updates)`);
  check(r.emu.resets >= 1, `board reset via USB control lines (${r.emu.resets} resets)`);
  check(!r.dev.opened, "USB port closed afterwards");
  console.log(`  (${r.ms} ms, ${r.logs.length} log lines)`);
}

// 2. Wrong chip: image built for ESP32-C3 must be refused before any write
{
  const bad = fw.slice(); bad[12] = 5; bad[13] = 0;
  const r = await run("image for a different chip", { images: [{ name: "c3.bin", data: bad, address: 0 }] });
  check(r.error && /ESP32-C3/.test(r.error.message), `refused: ${r.error?.message}`);
  check(!r.emu.log.includes(0x10), "no flash write command was sent");
}

// 3. Write corrupted in flash -> MD5 mismatch must be reported
{
  const r = await run("flash corruption detected", { emuOpts: { corruptWrite: true } });
  check(r.error && /MD5/i.test(r.error.message), `reported: ${r.error?.message}`);
}

// 4. Cable yanked mid-write -> clean error, no hang
{
  const r = await run("USB disconnect mid-write", { emuOpts: { dropAfterBytes: 200_000 } });
  check(!!r.error, `fails cleanly: ${String(r.error?.message).slice(0, 90)}`);
  check(r.ms < 30000, `doesn't hang (${r.ms} ms)`);
}

// 5. Erase-all option
{
  const r = await run("erase all first", { eraseAll: true });
  check(!r.error && r.emu.log.includes(0xd0), "erase-flash command sent and flash still verified");
}

// 6. App-only image at 0x10000 (custom file path)
{
  const r = await run("app image at 0x10000", { images: [{ name: "app.bin", data: app, address: 0x10000 }] });
  check(!r.error && Buffer.compare(Buffer.from(r.emu.flash.subarray(0x10000, 0x10000 + app.length)), Buffer.from(app)) === 0, "app written at 0x10000 and verified");
}

// 7. Pre-flight file checks used by the page
console.log("\n== pre-flight image checks");
check(inspectImage(fw, 0).ok && inspectImage(fw, 0).chipName === "ESP32-S3", "full-flash image accepted at 0x0, chip ESP32-S3");
check(!inspectImage(app, 0).ok, `app image at 0x0 refused: ${inspectImage(app, 0).error}`);
check(inspectImage(app, 0x10000).ok, "app image at 0x10000 accepted");
check(!inspectImage(new Uint8Array([1, 2, 3]), 0).ok, "random data at 0x0 refused");
check(!inspectImage(new Uint8Array(0), 0).ok, "empty file refused");

// 8. Device picker (Android WebUSB path)
console.log("\n== device picker");
{
  const seen = [];
  globalThis.isSecureContext = true;
  const esp = new FakeUsbDevice(new EspS3Emulator());
  Object.defineProperty(globalThis, "navigator", { configurable: true, value: { usb: { requestDevice: async (o) => { seen.push(o.filters); return esp; } } } });
  const logs = [];
  const port = await requestPort({ log: (l) => logs.push(l) });
  check(JSON.stringify(seen[0]) === JSON.stringify([{ vendorId: 0x303a }]), `Espressif picker filters by vendor only: ${JSON.stringify(seen[0])}`);
  check(port instanceof SerialPort, "returns a polyfilled serial port");
  check(logs.some((l) => /303a:1001/.test(l) && /CDC control/.test(l)), `logs device details: ${logs[0]}`);
  await requestPort({ anyDevice: true });
  check(JSON.stringify(seen[1]) === "[]", "'show every device' uses an empty filter");
  const hid = { vendorId: 0x1234, productId: 0x5678, productName: "Mouse", configurations: [{ interfaces: [{ interfaceNumber: 0, alternates: [{ interfaceClass: 3, endpoints: [] }] }] }] };
  navigator.usb.requestDevice = async () => hid;
  let err; try { await requestPort({ anyDevice: true }); } catch (e) { err = e; }
  check(err && /no USB serial interface/.test(err.message), `non-serial device rejected clearly: ${err?.message.slice(0, 70)}`);
}

console.log(failures ? `\n${failures} FAILED` : "\nALL PASSED");
process.exit(failures ? 1 : 0);

// Simulated ESP32-S3 behind a fake WebUSB device.
// Real stack under test: flasher-core.js -> esptool-js -> web-serial-polyfill -> (this mock USBDevice)
import zlib from "node:zlib";
import crypto from "node:crypto";

const C0 = 0xc0, DB = 0xdb;

export class EspS3Emulator {
  constructor({ corruptWrite = false, dropAfterBytes = Infinity } = {}) {
    this.flash = new Uint8Array(16 * 1024 * 1024).fill(0xff);
    this.stub = false;
    this.rx = [];            // bytes from host, partially decoded
    this.inFrame = false; this.esc = false;
    this.outQueue = [];      // Uint8Array chunks to host
    this.waiters = [];
    this.defl = null;
    this.log = [];
    this.corruptWrite = corruptWrite;
    this.bytesIn = 0; this.dropAfterBytes = dropAfterBytes; this.dropped = false;
    this.resets = 0;
    this.writes = [];        // {offset, size}
  }
  // ---- byte transport
  feed(bytes) {
    this.bytesIn += bytes.length;
    if (this.bytesIn > this.dropAfterBytes) { this.dropped = true; throw new Error("NetworkError: device was disconnected"); }
    for (const b of bytes) {
      if (!this.inFrame) { if (b === C0) { this.inFrame = true; this.rx = []; } continue; }
      if (this.esc) { this.rx.push(b === 0xdc ? C0 : b === 0xdd ? DB : b); this.esc = false; continue; }
      if (b === DB) { this.esc = true; continue; }
      if (b === C0) {
        if (this.rx.length) { this.handle(Uint8Array.from(this.rx)); this.inFrame = false; }
        continue; // C0 C0 back-to-back: stay in frame
      }
      this.rx.push(b);
    }
  }
  sendFrame(payload) {
    const out = [C0];
    for (const b of payload) { if (b === C0) out.push(DB, 0xdc); else if (b === DB) out.push(DB, 0xdd); else out.push(b); }
    out.push(C0);
    this.outQueue.push(Uint8Array.from(out));
    this.wake();
  }
  wake() { const w = this.waiters.splice(0); w.forEach((f) => f()); }
  async take(max) {
    while (!this.outQueue.length) await new Promise((r) => this.waiters.push(r));
    let chunk = this.outQueue[0];
    if (chunk.length > max) { this.outQueue[0] = chunk.slice(max); chunk = chunk.slice(0, max); } else this.outQueue.shift();
    return chunk;
  }
  // ---- protocol
  reply(op, val = 0, data = new Uint8Array(0), status = 0, err = 0) {
    const st = this.stub ? [status, err] : [status, err, 0, 0];
    const body = new Uint8Array(data.length + st.length);
    body.set(data); body.set(st, data.length);
    const p = new Uint8Array(8 + body.length);
    const dv = new DataView(p.buffer);
    p[0] = 1; p[1] = op; dv.setUint16(2, body.length, true); dv.setUint32(4, val >>> 0, true);
    p.set(body, 8);
    this.sendFrame(p);
  }
  handle(pkt) {
    if (pkt[0] !== 0) return;
    const op = pkt[1];
    const dv = new DataView(pkt.buffer, pkt.byteOffset);
    const len = dv.getUint16(2, true);
    const data = pkt.subarray(8, 8 + len);
    const u32 = (i) => new DataView(data.buffer, data.byteOffset).getUint32(i * 4, true);
    this.log.push(op);
    switch (op) {
      case 0x08: // SYNC: ROM answers 8 times
        for (let i = 0; i < (this.stub ? 1 : 8); i++) this.reply(op, 0x20120707);
        break;
      case 0x0a: { // READ_REG
        const addr = u32(0) >>> 0;
        let v = 0;
        if (addr === 0x40001000) v = 0x9; // magic (unused when security info works)
        this.reply(op, v);
        break;
      }
      case 0x14: { // GET_SECURITY_INFO (20-byte S3 layout)
        const d = new Uint8Array(20); const v = new DataView(d.buffer);
        v.setUint32(12, 9, true); // chip id ESP32-S3
        this.reply(op, 0, d);
        break;
      }
      case 0x09: case 0x0d: case 0x0b: case 0x05: case 0x07: case 0x02: case 0x04:
        this.reply(op);
        break;
      case 0x06: // MEM_END -> stub starts and greets
        this.reply(op);
        this.stub = true;
        setTimeout(() => this.sendFrame(new TextEncoder().encode("OHAI")), 5);
        break;
      case 0x10: { // FLASH_DEFL_BEGIN
        if (!this.stub) { this.reply(op, 0, undefined, 1, 5); break; }
        this.defl = { size: u32(0), offset: u32(3), chunks: [] };
        this.reply(op);
        break;
      }
      case 0x11: { // FLASH_DEFL_DATA
        const n = u32(0);
        this.defl.chunks.push(data.slice(16, 16 + n));
        this.commitDeflate(false);
        this.reply(op);
        break;
      }
      case 0x12: // FLASH_DEFL_END
        this.commitDeflate(true);
        this.reply(op);
        break;
      case 0x13: { // SPI_FLASH_MD5 (stub returns 16 raw bytes)
        const addr = u32(0), size = u32(1);
        const h = crypto.createHash("md5").update(this.flash.subarray(addr, addr + size)).digest();
        this.reply(op, 0, new Uint8Array(h));
        break;
      }
      case 0xd0: this.flash.fill(0xff); this.reply(op); break;
      default:
        this.reply(op, 0, undefined, 1, 5); // invalid command
    }
  }
  commitDeflate(final) {
    if (!this.defl) return;
    let raw;
    try { raw = zlib.inflateSync(Buffer.concat(this.defl.chunks)); }
    catch { if (!final) return; throw new Error("emulator: bad deflate stream"); }
    if (raw.length !== this.defl.size) { if (final) throw new Error(`emulator: size ${raw.length} != ${this.defl.size}`); return; }
    if (this.corruptWrite) raw[raw.length >> 1] ^= 0x55;
    this.flash.set(raw, this.defl.offset);
    this.writes.push({ offset: this.defl.offset, size: raw.length });
    this.defl = null;
  }
  reset() { this.resets++; this.stub = false; this.outQueue = []; }
}

// Fake WebUSB USBDevice shaped like the ESP32-S3 USB-Serial/JTAG peripheral (303a:1001).
export class FakeUsbDevice {
  constructor(emu) {
    this.emu = emu;
    this.vendorId = 0x303a; this.productId = 0x1001;
    this.opened = false; this.configuration = null;
    this.signals = [];
    const ep = (n, dir) => ({ endpointNumber: n, direction: dir, type: "bulk", packetSize: 64 });
    this.configurations = [{
      configurationValue: 1,
      interfaces: [
        { interfaceNumber: 0, alternates: [{ interfaceClass: 2, endpoints: [{ ...ep(1, "in"), type: "interrupt" }] }] },
        { interfaceNumber: 1, alternates: [{ interfaceClass: 10, endpoints: [ep(2, "in"), ep(2, "out")] }] },
        { interfaceNumber: 2, alternates: [{ interfaceClass: 255, endpoints: [ep(3, "in"), ep(4, "out")] }] },
      ],
    }];
  }
  async open() { this.opened = true; }
  async close() { this.opened = false; this.emu.wake(); }
  async selectConfiguration() { this.configuration = this.configurations[0]; }
  async claimInterface() {}
  async releaseInterface() {}
  async forget() {}
  async controlTransferOut(setup) {
    if (setup.request === 0x22) { // SET_CONTROL_LINE_STATE: DTR bit0, RTS bit1
      const dtr = !!(setup.value & 1), rts = !!(setup.value & 2);
      const prev = this.signals.at(-1);
      this.signals.push({ dtr, rts });
      // USB-Serial/JTAG: RTS asserted then released resets the chip
      if (prev && prev.rts && !rts) this.emu.reset();
    }
    return { status: "ok", bytesWritten: 0 };
  }
  async transferOut(ep, data) {
    if (!this.opened) throw new Error("NetworkError: device not opened");
    const bytes = new Uint8Array(data.buffer ?? data, data.byteOffset ?? 0, data.byteLength ?? data.length);
    this.emu.feed(bytes);
    return { status: "ok", bytesWritten: bytes.length };
  }
  async transferIn(ep, len) {
    if (this.emu.dropped) throw new Error("NetworkError: device was disconnected");
    const chunk = await Promise.race([
      this.emu.take(len),
      new Promise((r) => { const t = setInterval(() => { if (!this.opened || this.emu.dropped) { clearInterval(t); r(null); } }, 20); }),
    ]);
    if (!chunk) throw new Error("NetworkError: transfer cancelled");
    const ab = chunk.slice().buffer;
    return { status: "ok", data: new DataView(ab) };
  }
}

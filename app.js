import { transportSupport, requestPort, inspectImage, flashImages } from "./flasher-core.js";
import { md5Hex } from "./md5.js";

const $ = (id) => document.getElementById(id);
const state = { port: null, manifest: [], busy: false };

// ---------------------------------------------------------------- log
function log(line) {
  const t = new Date().toLocaleTimeString([], { hour12: false });
  const el = $("log");
  el.textContent += `[${t}] ${line}\n`;
  el.scrollTop = el.scrollHeight;
}
function setStatus(id, text, kind = "") {
  const el = $(id);
  el.textContent = text;
  el.className = "status" + (kind ? " " + kind : "");
}
$("copyLog").onclick = async () => {
  try { await navigator.clipboard.writeText($("log").textContent); $("copyLog").textContent = "Copied"; }
  catch { $("copyLog").textContent = "Copy failed. Long-press the log to select it."; }
  setTimeout(() => ($("copyLog").textContent = "Copy log"), 2000);
};

// ---------------------------------------------------------------- support
const support = transportSupport();
log(`USB route: ${support.kind}${support.reason ? " (" + support.reason + ")" : ""}; ${navigator.userAgent}`);
if (support.kind === "none") {
  const b = $("unsupported");
  b.hidden = false;
  b.innerHTML = support.reason === "insecure"
    ? "USB access needs a secure page. Open this flasher from its <b>https://</b> address."
    : "This browser can't talk to USB devices. Open this page in <b>Chrome</b> (Android or desktop). Firefox and Samsung Internet don't support it.";
  $("connect").disabled = true;
  $("connectAny").disabled = true;
}

// ---------------------------------------------------------------- connect
async function connect(anyDevice) {
  if (state.busy) return;
  try {
    const port = await requestPort({ anyDevice, log });
    state.port = port;
    const info = port.getInfo ? port.getInfo() : {};
    const id = info.usbVendorId !== undefined
      ? `USB ${info.usbVendorId.toString(16).padStart(4, "0")}:${(info.usbProductId ?? 0).toString(16).padStart(4, "0")}` : "device";
    log(`Selected ${id}`);
    const isEsp = info.usbVendorId === 0x303a;
    setStatus("connStatus", isEsp ? "Board selected. It will be checked when installing starts." : `Selected ${id}. This isn't an Espressif built-in USB port, but it may still work.`, isEsp ? "ok" : "warn");
    $("step2").querySelector(".step").classList.add("done");
  } catch (e) {
    if (e && (e.name === "NotFoundError" || /No device selected/i.test(e.message))) {
      log(`Picker closed with nothing chosen (${anyDevice ? "all devices" : "Espressif only"}).`);
      setStatus("connStatus", anyDevice
        ? "Your phone isn't seeing any USB device. Check the troubleshooting list below."
        : "No board chosen. If the list was empty, try \u201cShow every USB device\u201d below.", "warn");
      document.querySelector("details.small").open = true;
    } else {
      setStatus("connStatus", "Couldn't open USB: " + (e?.message ?? e), "bad");
      log("requestPort error: " + (e?.stack ?? e));
    }
  }
  refreshButton();
}
$("connect").onclick = () => connect(false);
$("connectAny").onclick = () => connect(true);

// ---------------------------------------------------------------- firmware
function fmtSize(n) { return n >= 1048576 ? (n / 1048576).toFixed(2) + " MB" : Math.round(n / 1024) + " KB"; }

async function loadManifest() {
  const list = $("fwList");
  try {
    const r = await fetch("firmware/manifest.json", { cache: "no-store" });
    if (!r.ok) throw new Error("HTTP " + r.status);
    state.manifest = (await r.json()).firmware ?? [];
  } catch (e) {
    log("manifest load failed: " + e);
    state.manifest = [];
  }
  list.innerHTML = "";
  if (!state.manifest.length) {
    list.innerHTML = '<p class="status warn">No bundled firmware found. You can still use your own .bin file.</p>';
  }
  state.manifest.forEach((fw, i) => {
    const lab = document.createElement("label");
    lab.className = "fwopt";
    lab.innerHTML = `<input type="radio" name="fw" value="m${i}"${i === 0 ? " checked" : ""}>
      <span class="fwtext"><b></b><span class="mute"></span><span class="mute"></span></span>`;
    const [b, m1, m2] = lab.querySelectorAll(".fwtext > *");
    b.textContent = `${fw.name} v${fw.version}`;
    m1.textContent = `${fw.board ?? fw.chip} · ${fmtSize(fw.size)}`;
    m2.textContent = fw.description ?? "";
    list.appendChild(lab);
  });
  if (!state.manifest.length) document.querySelector('input[value="custom"]').checked = true;
  onFwChange();
}

function selectedFw() { return document.querySelector('input[name="fw"]:checked')?.value ?? null; }
function onFwChange() {
  $("customBox").hidden = selectedFw() !== "custom";
  setStatus("fwStatus", "");
  if (selectedFw() !== null) $("step3").querySelector(".step").classList.add("done");
  refreshButton();
}
document.addEventListener("change", (e) => {
  if (e.target.name === "fw") onFwChange();
  if (e.target.id === "customFile" || e.target.id === "customAddr") { setStatus("fwStatus", ""); refreshButton(); }
});
$("customAddr").addEventListener("input", refreshButton);

function parseAddr(s) {
  const t = s.trim().toLowerCase();
  if (!/^(0x[0-9a-f]+|\d+)$/.test(t)) return null;
  const v = Number(t);
  return Number.isSafeInteger(v) && v % 0x1000 === 0 ? v : null; // flash sectors are 4 KB
}

async function gatherImages() {
  const sel = selectedFw();
  if (sel === "custom") {
    const f = $("customFile").files[0];
    if (!f) throw new Error("Choose a .bin file first.");
    const address = parseAddr($("customAddr").value);
    if (address === null) throw new Error("Flash address must be a multiple of 0x1000, like 0x0 or 0x10000.");
    const data = new Uint8Array(await f.arrayBuffer());
    return [{ name: f.name, data, address }];
  }
  const fw = state.manifest[Number(sel.slice(1))];
  setStatus("flashStatus", `Downloading ${fw.name}…`);
  const r = await fetch("firmware/" + fw.file, { cache: "no-store" });
  if (!r.ok) throw new Error(`Couldn't download ${fw.file} (HTTP ${r.status}).`);
  const data = new Uint8Array(await r.arrayBuffer());
  if (fw.size && data.length !== fw.size) throw new Error(`Download was incomplete (${data.length} of ${fw.size} bytes). Try again.`);
  if (fw.md5 && md5Hex(data) !== fw.md5) throw new Error("Downloaded firmware is corrupted (checksum mismatch). Try again.");
  log(`Downloaded ${fw.file}: ${data.length} bytes, md5 ok`);
  return [{ name: `${fw.name} v${fw.version}`, data, address: fw.address ?? 0 }];
}

function refreshButton() {
  const sel = selectedFw();
  const fwReady = sel && (sel !== "custom" || ($("customFile").files.length && parseAddr($("customAddr").value) !== null));
  $("flash").disabled = state.busy || !state.port || !fwReady;
  // Don't paint over a success or error message from the last attempt.
  if (!state.busy && !$("flash").disabled && !/\b(ok|bad)\b/.test($("flashStatus").className)) {
    setStatus("flashStatus", "Ready to install.");
  }
}

// ---------------------------------------------------------------- flash
function setProgress(f, kind = "") {
  $("bar").style.width = Math.round(Math.max(0, Math.min(1, f)) * 100) + "%";
  $("bar").parentElement.className = "progress" + (kind ? " " + kind : "");
}
const STAGE_TEXT = { connect: "Connecting to the chip…", erase: "Erasing flash…", write: "Writing", reset: "Restarting the board…" };

$("flash").onclick = async () => {
  if (state.busy) return;
  state.busy = true;
  $("after").hidden = true;
  $("flash").disabled = true;
  ["connect", "connectAny"].forEach((id) => ($(id).disabled = true));
  setProgress(0);
  let wakeLock = null;
  try { wakeLock = await navigator.wakeLock?.request("screen"); } catch { /* optional */ }

  try {
    const images = await gatherImages();
    for (const img of images) {
      const chk = inspectImage(img.data, img.address);
      if (!chk.ok) throw new Error(chk.error);
      for (const w of chk.warnings) {
        if (!confirm(w + "\n\nContinue anyway?")) throw new Error("Cancelled.");
      }
      log(`${img.name}: ${img.data.length} bytes → 0x${img.address.toString(16)}${chk.chipName ? " (" + chk.chipName + ")" : ""}`);
    }

    const result = await flashImages({
      port: state.port,
      images,
      eraseAll: $("eraseAll").checked,
      log,
      progress: (stage, f) => {
        if (stage === "write") {
          setProgress(f);
          setStatus("flashStatus", `Writing… ${Math.round(f * 100)}%  Keep the screen on and the cable still.`);
        } else {
          setStatus("flashStatus", STAGE_TEXT[stage] ?? stage);
        }
      },
    });

    setProgress(1, "ok");
    setStatus("flashStatus", result.resetOk
      ? "Installed and verified. The board has restarted."
      : "Installed and verified. Press RESET on the board (or unplug and replug) to start it.", "ok");
    $("step4").querySelector(".step").classList.add("done");
    $("after").hidden = false;
    // The board restarts, so this USB session is finished.
    state.port = null;
    setStatus("connStatus", "Disconnected after install. Reconnect to flash again.");
    $("step2").querySelector(".step").classList.remove("done");
  } catch (e) {
    const msg = e?.message ?? String(e);
    log("ERROR: " + (e?.stack ?? msg));
    setProgress(1, "bad");
    let hint = "";
    if (/timeout|timed out|sync|failed to connect|wrong boot mode/i.test(msg)) {
      hint = " Unplug the board, hold BOOT while plugging it back in, reconnect in step 2 and try again.";
      state.port = null;
      $("step2").querySelector(".step").classList.remove("done");
      setStatus("connStatus", "Reconnect the board.", "warn");
    } else if (/disconnected|device was lost|NetworkError|transfer error|USB error|stream stopped/i.test(msg)) {
      hint = " The USB connection dropped. Check the cable, then reconnect in step 2.";
      state.port = null;
      $("step2").querySelector(".step").classList.remove("done");
    }
    setStatus("flashStatus", msg + hint, "bad");
  } finally {
    try { await wakeLock?.release(); } catch { /* ignore */ }
    state.busy = false;
    if (support.kind !== "none") ["connect", "connectAny"].forEach((id) => ($(id).disabled = false));
    refreshButton();
  }
};

navigator.usb?.addEventListener?.("disconnect", () => {
  if (!state.busy && state.port) {
    state.port = null;
    setStatus("connStatus", "Board unplugged. Reconnect it to continue.", "warn");
    $("step2").querySelector(".step").classList.remove("done");
    refreshButton();
  }
});

loadManifest();

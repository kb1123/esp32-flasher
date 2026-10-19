# ESP32 Flasher

A phone-friendly web page for installing ESP32 firmware over USB, from Chrome on Android (or desktop).

- **Android:** uses WebUSB plus Google's [web-serial-polyfill](https://github.com/google/web-serial-polyfill) to talk to the ESP32-S3's built-in USB port.
- **Desktop Chrome/Edge:** uses Web Serial directly.
- Flashing is done by Espressif's [esptool-js](https://github.com/espressif/esptool-js) 0.7.0, with every write verified by MD5.

## Use it

1. Hold **BOOT** on the board, plug it into your phone with a USB-C OTG cable, release BOOT.
2. Open this site in Chrome and tap **Connect to board**.
3. Pick the bundled firmware (or your own `.bin` and address), tap **Install firmware**.

## Hosting

This must be served over HTTPS (WebUSB requirement). GitHub Pages works: Settings → Pages → Deploy from branch → `main` / root.

## Adding firmware

Put the `.bin` in `firmware/` and add an entry to `firmware/manifest.json` with its `size` and `md5`. The page re-checks both after downloading, so a truncated download can never be flashed.

## Layout

| Path | What |
| --- | --- |
| `index.html`, `style.css`, `app.js` | The page |
| `flasher-core.js` | Connect / write / verify / reset logic (no DOM; unit-testable) |
| `md5.js` | MD5 for post-write verification |
| `lib/esptool/` | esptool-js 0.7.0 compiled to plain ES modules (Apache-2.0; pako MIT) |
| `lib/polyfill/` | web-serial-polyfill 1.0.15 (Apache-2.0) |
| `firmware/` | Bundled images + manifest |

## Firmware

| Path | What |
| --- | --- |
| `firmware-src/libraries/OtaCore/` | Shared library every firmware links: Wi-Fi hotspot with captive portal, home Wi-Fi, Ethernet, the update page at `/update`, automatic rollback, and switching back to the firmware in the other slot. |
| `firmware-src/ota-base/` | The starting firmware: just OtaCore plus a status LED. Copy it to start a new project. |
| `firmware-src/camera-viewer/` | Live camera feed on the hotspot's front page (MJPEG on port 81), with snapshot and picture controls. |
| `firmware-src/build.sh` | `./build.sh <sketch-folder>` builds the `-OTA.bin` and the `-FULL-FLASH-0x0.bin`. |
| `builds/` | Ready-to-upload `-OTA.bin` files. |

New project checklist: copy `ota-base`, change `OTA_FIRMWARE("name", "version")`, keep `otaCoreBegin()` / `otaCoreLoop()`, never block `loop()` for long, and set `appOwnsRoot = true` if your app serves `/`.

## Tests

`node tests/run.mjs` (Node 22+) runs the real flasher code against a simulated ESP32-S3 over a fake WebUSB device.

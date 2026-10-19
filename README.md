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

## Also in this repo

- `firmware-src/ota-base/`: Arduino source of the bundled OTA base firmware v1.1.0: Wi-Fi hotspot with captive portal, optional home Wi-Fi, Ethernet (Waveshare ESP32-S3-ETH, arduino-esp32 3.3.12, 16 MB flash, OPI PSRAM, two 3 MB OTA slots).
- `tests/`: runs the real flasher code against a simulated ESP32-S3 over a fake WebUSB device: `node tests/run.mjs` (Node 22+).

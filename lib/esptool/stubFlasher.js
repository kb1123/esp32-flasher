import atob from "./atob-lite.js";
/**
 * Import flash stub json for the given chip name.
 * @param {string} chipName Name of chip to obtain flasher stub
 * @param {number} chipRevision Chip revision number
 * @returns {Stub} Stub information and decoded text and data
 */
export async function getStubJsonByChipName(chipName, chipRevision) {
    let jsonStub;
    switch (chipName) {
        case "ESP32":
            jsonStub = await fetch(new URL("./targets/stub_flasher/esp32.json", import.meta.url)).then((r) => r.json());
            break;
        case "ESP32-C2":
            jsonStub = await fetch(new URL("./targets/stub_flasher/esp32c2.json", import.meta.url)).then((r) => r.json());
            break;
        case "ESP32-C3":
            jsonStub = await fetch(new URL("./targets/stub_flasher/esp32c3.json", import.meta.url)).then((r) => r.json());
            break;
        case "ESP32-C5":
            jsonStub = await fetch(new URL("./targets/stub_flasher/esp32c5.json", import.meta.url)).then((r) => r.json());
            break;
        case "ESP32-C6":
            jsonStub = await fetch(new URL("./targets/stub_flasher/esp32c6.json", import.meta.url)).then((r) => r.json());
            break;
        case "ESP32-C61":
            jsonStub = await fetch(new URL("./targets/stub_flasher/esp32c61.json", import.meta.url)).then((r) => r.json());
            break;
        case "ESP32-H2":
            jsonStub = await fetch(new URL("./targets/stub_flasher/esp32h2.json", import.meta.url)).then((r) => r.json());
            break;
        case "ESP32-H4":
            jsonStub = await fetch(new URL("./targets/stub_flasher/esp32h4.json", import.meta.url)).then((r) => r.json());
            break;
        case "ESP32-H21":
            // No stub is published in esptool stub_flasher/2 yet.
            break;
        case "ESP32-E22":
            // No stub is published in esptool stub_flasher/2 yet.
            break;
        case "ESP32-P4":
            if (chipRevision && chipRevision < 300) {
                jsonStub = await fetch(new URL("./targets/stub_flasher/esp32p4-rev1.json", import.meta.url)).then((r) => r.json());
            }
            else {
                jsonStub = await fetch(new URL("./targets/stub_flasher/esp32p4.json", import.meta.url)).then((r) => r.json());
            }
            break;
        case "ESP32-S2":
            jsonStub = await fetch(new URL("./targets/stub_flasher/esp32s2.json", import.meta.url)).then((r) => r.json());
            break;
        case "ESP32-S3":
            jsonStub = await fetch(new URL("./targets/stub_flasher/esp32s3.json", import.meta.url)).then((r) => r.json());
            break;
        case "ESP32-S31":
            jsonStub = await fetch(new URL("./targets/stub_flasher/esp32s31.json", import.meta.url)).then((r) => r.json());
            break;
        case "ESP8266":
            jsonStub = await fetch(new URL("./targets/stub_flasher/esp8266.json", import.meta.url)).then((r) => r.json());
            break;
    }
    if (jsonStub) {
        const stub = "default" in jsonStub ? jsonStub.default : jsonStub;
        return {
            bss_start: stub.bss_start,
            data: stub.data,
            data_start: stub.data_start,
            entry: stub.entry,
            text: stub.text,
            text_start: stub.text_start,
            decodedData: decodeBase64Data(stub.data),
            decodedText: decodeBase64Data(stub.text),
        };
    }
    return;
}
/**
 * Convert a base 64 string to Uint8Array.
 * @param {string} dataStr Base64 String to decode
 * @returns {Uint8Array} Decoded Uint8Array
 */
export function decodeBase64Data(dataStr) {
    const decoded = atob(dataStr);
    const chardata = decoded.split("").map(function (x) {
        return x.charCodeAt(0);
    });
    return new Uint8Array(chardata);
}

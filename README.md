# Matter + Apple HomeKey Dual-Partition Smart Lock

This project implements a fully functional Matter Smart Lock with Apple HomeKey NFC support on a single ESP32-C3/C6 microcontroller. 

Apple currently only issues HomeKey NFC certificates to native HomeKit (HAP) accessories, and **not** to Matter accessories. To solve this limitation while maintaining Matter ecosystem interoperability, this project utilizes a novel **dual-partition architecture**.

## The Vision & Architecture
This firmware splits the device's lifecycle into two distinct OTA partitions:

### 1. `ota_1`: The HomeKey Provisioner (Trojan Horse)
Built using the [HomeSpan](https://github.com/HomeSpan/HomeSpan) framework, this temporary partition exposes the device as a native Apple HomeKit Lock. Its sole purpose is to securely negotiate with the Apple Home app, complete the HomeKit pairing process, and extract the HomeKey cryptography certificates (Reader Private Key, Issuer Keys, and Endpoint Public Keys) from Apple. These certificates are securely persisted into the device's Non-Volatile Storage (NVS) partition. Once the certificates are successfully captured, the provisioner forces a hardware reboot into the main Matter runtime.

### 2. `ota_0`: The Main Matter Runtime
Built using the [ESP-Matter](https://github.com/espressif/esp-matter) SDK, this partition exposes the device as a standard, universal Matter Smart Lock. The Matter runtime continuously runs a background NFC polling task (via a PN532 module) that implements the Apple HomeKey ISO-DEP protocol. When an Apple Wallet tap occurs, the NFC thread utilizes the cryptography certificates extracted earlier (from NVS) to authenticate the iPhone/Watch entirely offline. Upon successful NFC authentication, it triggers a local unlock event in the Matter data model.

You get the best of both worlds: Universal Matter connectivity for remote app control across Apple/Google/Amazon, paired with local Apple HomeKey NFC unlocks.

> [!WARNING]
> ### The "Offline Lock" Caveat
> When the device reboots from the `ota_1` provisioner back into the `ota_0` Matter runtime, the original HomeKit Lock you paired will show as **"No Response"** in the Apple Home app forever. This is expected, as the original HAP server is no longer running.
> 
> You will pair the lock *again* using the Matter QR code, which will create a second, fully responsive lock. 
> 
> **CRITICAL**: Do **NOT** delete the "No Response" HomeKit lock! Deleting it will permanently revoke the HomeKey pass from your Apple Wallet. The offline lock acts as an "Anchor" for the certificates. The recommended workaround is to rename the offline lock to *"NFC Key Anchor (Do Not Delete)"* and move it to a dummy room named *"Hidden"* or *"System"*. The Apple Wallet NFC unlock is purely offline and will continue to work perfectly despite the anchor lock showing "No Response".

## Features
- **Matter Door Lock Server**: Full interoperability with any Matter ecosystem.
- **Apple HomeKey ISO-DEP Engine**: Offline NFC cryptography to authenticate Apple Wallet passes using `DigitalDoorKey` ECP flows.
- **Dual-Boot Provisioning**: Automated switching between `ota_1` (HomeKit) and `ota_0` (Matter).
- **Contiguous Memory Serialization**: Hardened TLV8 serialization engine to prevent heap corruption and Watchdog aborts on memory-constrained (400KB SRAM) RISC-V targets.
- **ESP32-C3 / ESP32-C6 Support**: Tested on both Wi-Fi 4 and Wi-Fi 6 chips.

## Hardware Setup (Defaults)
By default, the following GPIOs are used (ESP32-C3 example):
- **NFC Module**: PN532 (SPI Mode)
  - `SS`: GPIO 4
  - `SCK`: GPIO 1
  - `MISO`: GPIO 2
  - `MOSI`: GPIO 3
- **Provisioner Trigger / BOOT Button**: GPIO 9 (Active Low)
- **Relay Pin**: GPIO 8
- **Status LED Pin**: GPIO 5

### Web-Based Hardware Configuration
You do not need to recompile the firmware to change GPIO pins! A lightweight HTTP server runs natively on the device at port `8080` to allow runtime configuration.
1. Connect the ESP32 to your Wi-Fi network (or connect your phone to the provisioner SoftAP).
2. Open a web browser and navigate to `http://<device-ip>:8080`.
3. You can dynamically modify the SPI pins, the boot button, the relay pin, the status LED, and configure the **Relay Active Level** (High/Low) to match your physical relay logic.
4. Click "Save & Reboot" to apply the changes. Settings are persisted to NVS automatically.

## Build and Setup Instructions
Ensure you have ESP-IDF v5.4.4 and ESP-Matter configured in your environment.

### 1. Prepare the Build Environment
```bash
# Set your target (esp32c3 or esp32c6)
idf.py set-target esp32c3
```

### Using Pre-Compiled CI Binaries (Recommended for Updating)
If you just want to update your firmware and don't want to set up the ESP-Matter build environment, you can download the latest pre-compiled binaries from the **GitHub Releases** page.
You can flash these binaries directly to your ESP32 using `esptool.py`. 

**What happens to the NVS partition?**
The NVS partition (which stores your Wi-Fi credentials, Apple HomeKey certificates, and Matter pairing data) is located at offset `0x10000`. When you flash the specific application binaries to their respective offsets (`0x20000` and `0x210000`), **the NVS partition is completely untouched and safe**. You will not lose your keys or need to repair the lock.

```bash
# Ensure you have esptool installed
pip install esptool

# For an ALREADY setup board (Updates Matter & Provisioner without touching NVS):
# Replace <target> with esp32c3 or esp32c6
esptool.py -p /dev/tty.usbmodem* -b 460800 write_flash \
  0x20000 matter_lock_homekit_ota0_<target>.bin \
  0x210000 provisioner_ota1_<target>.bin

# For a BRAND NEW board (Flashes everything in one go):
# The 'full' binary contains the Bootloader, Partition Table, ota_0, and ota_1 combined into a single file!
esptool.py -p /dev/tty.usbmodem* -b 460800 write_flash 0x0 matter_lock_homekit_full_<target>.bin
```

### 2. Build the Dual Firmware
Both the Matter app and the Provisioner app must be compiled for your target architecture.
```bash
# Build the main Matter runtime (ota_0)
idf.py build

# Build the HomeKit provisioner (ota_1)
cd provisioner
idf.py build
cd ..
```

### 3. Flash the Device
Erase the flash to clear any stale NVS data, then flash the bootloader, partition table, and the main Matter app.
```bash
idf.py erase-flash
idf.py flash monitor
```

### 4. Flash the Provisioner (ota_1)
In a new terminal window, use the included shell script to write the HomeSpan provisioner binary directly into the `ota_1` offset defined in `partitions.csv` (0x210000).
```bash
cd provisioner
./flash_to_ota1.sh /dev/cu.usbserial-XXXXXX
```

### 5. Provisioning Flow
1. **Trigger the Provisioner**: Boot the device and hold the BOOT button (GPIO 9) for 5 seconds. The device will reboot into `ota_1` (The Temporary HomeKey Provisioner).
2. **Pair to Apple Home**: The device will broadcast a Wi-Fi AP named `MatterLock-HomeKey`. Connect your iPhone to it.
3. Open the Apple Home app and add the accessory using the setup code `466-37-726`.
4. Wait for the Home app to generate the HomeKey in your Apple Wallet. The `pair_callback` will automatically extract the Apple Home Issuer Key and save it securely to NVS.
5. **Return to Matter**: Hold the BOOT button again for 3 seconds. The device will reboot back to the Matter runtime (`ota_0`).
6. Pair the lock into Apple Home *again*, this time using the printed Matter setup code.

## Roadmap & Future Enhancements

- **Secure Boot V2 & Flash Encryption:** Enabling hardware flash encryption to prevent physical extraction of Apple HomeKey cryptography certificates from NVS.
- **Deep Sleep Optimization:** Implement PN532 low-power IRQ wake-up or capacitive touch wake-up to minimize power draw for battery-operated deployments.
- **Matter Battery Cluster:** Expose the ESP32's ADC reading as a Matter Battery Level cluster for visibility in the Apple/Google Home app.
- **mDNS / Bonjour for Web Config:** Allow access to the configuration portal via a `.local` hostname instead of an IP address.
- **Dynamic Wi-Fi Provisioning:** Allow Wi-Fi credentials to be updated over BLE or the web portal without erasing NVS.
- **Custom PCB Design:** Provide a KiCad/EasyEDA design for a clean, jumper-free integration of the ESP32, Relay, and PN532.

## Credits

This project uses and builds around HomeKey/NFC work from the following GitHub repositories:

- [HomeKey-ESP32](https://github.com/rednblkx/HomeKey-ESP32) by rednblkx, for the broader ESP32 Home Key project and architecture reference.
- [HK-HomeKit-Lib / DigitalDoorKey](https://github.com/rednblkx/HK-HomeKit-Lib) by rednblkx, used for Home Key / Digital Key authentication and provisioning logic.
- [pn532_cxx](https://github.com/rednblkx/pn532_cxx) and [esp-hal-pn532](https://github.com/rednblkx/esp-hal-pn532) by rednblkx, used for PN532 NFC integration.
- [HomeSpan](https://github.com/HomeSpan/HomeSpan), used for the experimental HomeKit provisioning accessory.

## Disclaimers & Licensing

**MIT License**

Copyright (c) 2026 Adwait Kale

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

### Legal Disclaimer
**HomeKey**, **Apple Wallet**, **HomeKit**, and **Apple Home** are registered trademarks of Apple Inc. 
This project is an independent, experimental, open-source endeavor created strictly for educational and research purposes. It relies on reverse-engineered ISO-DEP and ECP protocols.
- This software is **NOT** certified by, endorsed by, or affiliated with Apple Inc., the Connectivity Standards Alliance (CSA), or the Matter protocol.
- Do not use this software in production, commercial products, or physical security applications. It lacks secure enclave hardware integration and Apple MFi certification.
- Use at your own risk. The author assumes no liability for lockouts, security breaches, or damage to property.

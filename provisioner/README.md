# Home Key Provisioner

This app is the temporary Home Key enrollment firmware that lives in the main
firmware's `ota_1` slot.

Runtime flow:

1. Main Matter firmware runs from `ota_0`.
2. User long-presses `BOOT` for 5 seconds.
3. Main firmware sets the boot-mode flag, switches to `ota_1`, and reboots.
4. This provisioner app starts HomeSpan with the Home Key `NFCAccess` service.
5. Apple Home writes Home Key TLV8 requests to the access control point.
6. The provisioner forwards those TLVs into `homekey_provisioning_core`.
7. Provisioned reader data is persisted to NVS in the shared `readerData_t` format.
8. Once the reader data becomes provisioned, the provisioner waits 10 seconds,
   switches back to `ota_0`, and reboots into the Matter runtime.

What is working now:

- Shared provisioning/storage core is wired in.
- Boot-mode handoff between `ota_0` and `ota_1` is implemented.
- HomeSpan exposes the Home Key access control point and forwards TLV8 payloads
  into `HomeKeyProvisioningMgr().ProcessRequest(...)`.
- The provisioner reuses stored Wi-Fi credentials when available, otherwise it
  falls back to a HomeSpan AP named `MatterLock-HomeKey`.

Build and deploy:

1. Build the provisioner:
   `idf.py build`
2. Flash the main firmware normally so its partition table owns `ota_0` and `ota_1`.
3. Flash only the provisioner image into `ota_1`:
   `./flash_to_ota1.sh <serial-port>`

Notes:

- `flash_to_ota1.sh` writes only `build/homekey_provisioner.bin` to offset
  `0x210000`, which matches the main firmware's `ota_1` partition.
- On startup the provisioner logs the Home Key setup code and fallback AP SSID.
- The current Home Key setup code comes from
  `HomeKeyProvisioningMgr().GetSetupCode()` and is currently `46637726`.

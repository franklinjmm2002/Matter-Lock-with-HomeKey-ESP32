# Home Key Provisioning Plan

This app now treats Apple Home Key provisioning as a separate concern from the
runtime Matter + NFC lock flow.

## Stable Runtime Boundary

The in-firmware runtime path is already in place:

- `HomeKeyNfcManager::HandleHomeKeyAuth()` reads persisted reader state.
- `DDKAuthenticationContext` uses that state to authenticate a tap.
- `HomeKeyMgr().UnlockAuthenticatedHomeKey(...)` unlocks only after a valid
  authenticated Home Key flow.

The missing piece is not NFC or unlock logic. The missing piece is a transport
that can provision valid `readerData_t` into NVS.

## Provisioning Contract

Any future provisioning transport must populate the existing
`HomeKeyReaderDataManager` data model and save it through
`HomeKeyReaderDataMgr().UpdateReaderData(...)`.

Minimum required fields in `readerData_t`:

- `reader_sk`: reader private key
- `reader_pk`: derived reader public key
- `reader_pk_x`: x-coordinate of the reader public key
- `reader_id`: reader identifier
- `reader_gid`: reader group/key identifier
- `issuers[]`: trusted issuers allowed to provision and authenticate endpoints

Minimum required fields per `hkIssuer_t`:

- `issuer_id`
- `issuer_pk`
- `issuer_pk_x`
- `endpoints[]`

Minimum required fields per `hkEndpoint_t`:

- `endpoint_id`
- `endpoint_pk`
- `endpoint_pk_x`
- `key_type`
- `counter`
- `last_used_at`

The data is already persisted in NVS under:

- namespace: `SAVED_DATA`
- key: `HK_READERDATA`

## Expected Provisioning Operations

The DigitalDoorKey HomeKit helper already describes the operations we need to
support. A future provisioning transport should be able to handle:

1. Reader key write
   - Supply reader private key and reader identifier.
   - Derive `reader_pk`, `reader_pk_x`, and `reader_gid`.
   - Persist the updated `readerData_t`.

2. Reader key read
   - Return the current reader key identifier if a reader is provisioned.

3. Device credential write
   - Accept issuer identifier and endpoint public key.
   - Match the issuer against `readerData_t.issuers`.
   - Create a new `hkEndpoint_t` if the endpoint does not already exist.
   - Persist the updated issuer/endpoints list.

4. Reader key remove
   - Clear the reader identity and persisted reader key material.

These semantics already exist in `components/DigitalDoorKey/src/HK_HomeKit.cpp`.

## Good Integration Options

Any provisioning path is acceptable if it writes the same persisted model.
Examples:

- A separate Apple-approved HomeKit provisioning firmware
- A temporary BLE or local HTTP provisioning endpoint
- A serial or console-based manufacturing/provisioning tool
- An external commissioner app that sends the same TLV payloads

The transport should stay separate from the lock runtime so the main firmware
can remain Matter + NFC/Home Key focused.

## Suggested Implementation Order

1. Keep this app on the current stubbed provisioning manager.
2. Use the existing console helpers to validate `readerData_t` persistence.
3. Introduce a dedicated provisioning transport that only maps incoming TLVs
   into `readerData_t`.
4. Reuse `HK_HomeKit` or extract its TLV mutation logic into a transport-agnostic
   helper instead of coupling it to Arduino/HomeSpan again.

## Useful Validation Hooks

The console already exposes useful probes:

- `homekey status`
- `homekey dump`
- `homekey set-reader <reader_private_key_hex> <reader_identifier_hex>`
- `homekey add-issuer <issuer_ed25519_public_key_hex>`
- `homekey set-reader-tlv <reader_private_key_hex> <reader_identifier_hex>`
- `homekey read-reader-tlv`
- `homekey add-endpoint <issuer_id_hex> <endpoint_public_key_hex> <key_type>`
- `homekey process-tlv <homekey_tlv_hex>`
- `homekey erase`

These commands are enough to validate that:

- the reader becomes provisioned,
- issuer material persists,
- endpoint enrollment updates the issuer list,
- the runtime can see the same state after reboot.

## Non-Goals For The Main Firmware

This app should not depend on HomeSpan or Arduino just to satisfy the runtime
Home Key path. Those stacks pulled in an IPv4 requirement that conflicts with
the current CHIP ESP32 configuration for this build.

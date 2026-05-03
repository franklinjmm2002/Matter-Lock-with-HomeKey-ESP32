#!/usr/bin/env bash

set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "Usage: $0 <serial-port> [esptool args...]"
  echo "Example: $0 /dev/cu.usbmodem1101"
  exit 1
fi

PORT="$1"
shift || true

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
IMAGE_PATH="${BUILD_DIR}/homekey_provisioner.bin"
OTA1_OFFSET="0x210000"

if [[ ! -f "${IMAGE_PATH}" ]]; then
  echo "Missing ${IMAGE_PATH}. Run 'idf.py build' in ${SCRIPT_DIR} first."
  exit 1
fi

python -m esptool \
  --chip esp32c3 \
  -p "${PORT}" \
  -b 460800 \
  --before default_reset \
  --after hard_reset \
  write_flash \
  "${OTA1_OFFSET}" \
  "${IMAGE_PATH}" \
  "$@"

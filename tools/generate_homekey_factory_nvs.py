#!/usr/bin/env python3
"""
Generate a minimal or merged NVS image that carries the Home Key issuer public key
in the `chip-factory` namespace before first boot.

The runtime looks for the issuer in the following order:
1. HK_FACTORY / ISSUER_PUBKEY
2. chip-factory / hk-issuer-pk (blob) or hk-issuer-pk-hex (string)
3. CONFIG_HOMEKEY_FACTORY_ISSUER_PUBKEY_HEX

This tool is intended for manufacturing or bring-up workflows where a prebuilt NVS
image should already contain the trusted issuer before ota_0 or ota_1 boots.
"""

from __future__ import annotations

import argparse
import csv
import re
import subprocess
import sys
from pathlib import Path
from typing import List

try:
    from cryptography.hazmat.primitives.asymmetric import ed25519
    from cryptography.hazmat.primitives import serialization
    HAS_CRYPTOGRAPHY = True
except ImportError:
    HAS_CRYPTOGRAPHY = False



PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PARTITIONS_CSV = PROJECT_ROOT / "partitions.csv"
IDF_NVS_GEN = (
    Path.home()
    / ".espressif"
    / "v5.4.4"
    / "esp-idf"
    / "components"
    / "nvs_flash"
    / "nvs_partition_generator"
    / "nvs_partition_gen.py"
)

HEADER = ["key", "type", "encoding", "value"]
CHIP_FACTORY_NAMESPACE = "chip-factory"
ISSUER_BLOB_KEY = "hk-issuer-pk"
ISSUER_HEX_KEY = "hk-issuer-hex"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate a Home Key chip-factory NVS image for manufacturing."
    )
    parser.add_argument(
        "--issuer-pubkey-hex",
        required=False,
        help="32-byte Ed25519 issuer public key as 64 hex characters. If omitted, a random keypair is generated.",
    )
    parser.add_argument(
        "--partitions-csv",
        type=Path,
        default=DEFAULT_PARTITIONS_CSV,
        help="Partition table used to derive the NVS partition size.",
    )
    parser.add_argument(
        "--partition-label",
        default="nvs",
        help="Partition label to generate. Defaults to the shared runtime NVS partition.",
    )
    parser.add_argument(
        "--base-csv",
        type=Path,
        help="Optional existing manufacturing CSV to merge into. If omitted, a minimal chip-factory CSV is created.",
    )
    parser.add_argument(
        "--namespace",
        default=CHIP_FACTORY_NAMESPACE,
        help="Factory namespace to populate. Defaults to chip-factory.",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=PROJECT_ROOT / "tools" / "out" / "homekey_factory_nvs",
        help="Directory where the generated CSV and binary will be written.",
    )
    parser.add_argument(
        "--csv-name",
        default="homekey_chip_factory.csv",
        help="Output CSV filename.",
    )
    parser.add_argument(
        "--bin-name",
        default="homekey_chip_factory_nvs.bin",
        help="Output NVS binary filename.",
    )
    parser.add_argument(
        "--no-bin",
        action="store_true",
        help="Only write the CSV. Skip binary generation.",
    )
    return parser.parse_args()


def normalize_pubkey_hex(raw: str) -> str:
    compact = re.sub(r"\s+", "", raw)
    if compact.lower().startswith("0x"):
        compact = compact[2:]
    if not re.fullmatch(r"[0-9a-fA-F]{64}", compact):
        raise ValueError("issuer public key must be exactly 32 bytes / 64 hex characters")
    return compact.upper()


def parse_partition_size(partitions_csv: Path, partition_label: str) -> tuple[int, str]:
    with partitions_csv.open(newline="") as fp:
        for raw_line in fp:
            line = raw_line.strip()
            if not line or line.startswith("#"):
                continue
            columns = [part.strip() for part in raw_line.split(",")]
            if len(columns) < 5:
                continue
            label = columns[0]
            offset = columns[3] or "<auto>"
            size = columns[4]
            if label == partition_label:
                return int(size, 0), offset
    raise ValueError(f"partition label '{partition_label}' not found in {partitions_csv}")


def load_rows(base_csv: Path | None) -> List[List[str]]:
    if base_csv is None:
        return [HEADER.copy()]

    with base_csv.open(newline="") as fp:
        rows = [row for row in csv.reader(fp)]

    if not rows:
        raise ValueError(f"{base_csv} is empty")
    if rows[0] != HEADER:
        raise ValueError(f"{base_csv} must start with header: {','.join(HEADER)}")
    return rows


def upsert_namespace_rows(rows: List[List[str]], namespace: str, issuer_hex: str) -> List[List[str]]:
    if not rows:
        rows = [HEADER.copy()]

    filtered: List[List[str]] = [rows[0]]
    namespace_seen = False
    for row in rows[1:]:
        if not row:
            continue
        key = row[0].strip()
        row_type = row[1].strip() if len(row) > 1 else ""
        if row_type == "namespace":
            filtered.append([key, "namespace", "", ""])
            namespace_seen = namespace_seen or (key == namespace)
            continue
        if key in (ISSUER_BLOB_KEY, ISSUER_HEX_KEY):
            continue
        filtered.append(row)

    if not namespace_seen:
        filtered.append([namespace, "namespace", "", ""])

    insert_at = None
    for idx, row in enumerate(filtered):
        if len(row) >= 2 and row[0] == namespace and row[1] == "namespace":
            insert_at = idx + 1
    if insert_at is None:
        raise AssertionError("namespace insertion point not found")

    issuer_rows = [
        [ISSUER_BLOB_KEY, "data", "hex2bin", issuer_hex],
        [ISSUER_HEX_KEY, "data", "string", issuer_hex],
    ]
    return filtered[:insert_at] + issuer_rows + filtered[insert_at:]


def write_csv(rows: List[List[str]], path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="") as fp:
        writer = csv.writer(fp, lineterminator="\n")
        writer.writerows(rows)


def generate_nvs_bin(csv_path: Path, bin_path: Path, size: int) -> None:
    if not IDF_NVS_GEN.exists():
        raise FileNotFoundError(f"ESP-IDF NVS generator not found: {IDF_NVS_GEN}")

    cmd = [
        sys.executable,
        str(IDF_NVS_GEN),
        "generate",
        str(csv_path),
        str(bin_path),
        hex(size),
    ]
    subprocess.run(cmd, check=True)


def generate_keypair() -> tuple[str, str]:
    if not HAS_CRYPTOGRAPHY:
        raise ImportError("The 'cryptography' Python package is required to generate a random keypair. Run 'pip install cryptography' or provide the key manually.")
    
    private_key = ed25519.Ed25519PrivateKey.generate()
    public_key = private_key.public_key()
    
    priv_bytes = private_key.private_bytes(
        encoding=serialization.Encoding.Raw,
        format=serialization.PrivateFormat.Raw,
        encryption_algorithm=serialization.NoEncryption()
    )
    pub_bytes = public_key.public_bytes(
        encoding=serialization.Encoding.Raw,
        format=serialization.PublicFormat.Raw
    )
    
    return priv_bytes.hex().upper(), pub_bytes.hex().upper()


def main() -> int:
    args = parse_args()
    
    issuer_hex = ""
    issuer_priv_hex = None
    
    if args.issuer_pubkey_hex:
        issuer_hex = normalize_pubkey_hex(args.issuer_pubkey_hex)
    else:
        print("No --issuer-pubkey-hex provided. Generating a random Ed25519 keypair...")
        issuer_priv_hex, issuer_hex = generate_keypair()
        print("\n" + "=" * 60)
        print("ATTENTION: Save this Private Key if you need to sign endpoint")
        print("enrollments later (e.g., to create an Apple Wallet pass)!")
        print(f"Issuer Private Key: {issuer_priv_hex}")
        print(f"Issuer Public Key:  {issuer_hex}")
        print("=" * 60 + "\n")

    partition_size, partition_offset = parse_partition_size(args.partitions_csv, args.partition_label)

    rows = load_rows(args.base_csv)
    rows = upsert_namespace_rows(rows, args.namespace, issuer_hex)

    csv_path = args.output_dir / args.csv_name
    bin_path = args.output_dir / args.bin_name
    write_csv(rows, csv_path)

    print(f"Wrote CSV: {csv_path}")
    print(f"Partition label: {args.partition_label}")
    print(f"Partition offset: {partition_offset}")
    print(f"Partition size: 0x{partition_size:X}")
    print(f"Namespace: {args.namespace}")
    print(f"Keys: {ISSUER_BLOB_KEY}, {ISSUER_HEX_KEY}")

    if not args.no_bin:
        generate_nvs_bin(csv_path, bin_path, partition_size)
        print(f"Wrote NVS binary: {bin_path}")
        print()
        print("Flash command:")
        print(
            f"  esptool.py -p <PORT> write_flash {partition_offset} {bin_path}"
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

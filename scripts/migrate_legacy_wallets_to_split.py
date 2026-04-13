#!/usr/bin/env python3
"""Migrate all rows from legacy recovered_wallets into split tables + XLSX export.

Flow:
1. Read every row from legacy table (ORDER BY id).
2. Append rows to XLSX sheets: all/btc/evm/sol/unknown.
3. For each row (one-by-one):
   - insert mnemonic into seed_phrases_{btc|evm|sol}
   - insert wallet into recovered_wallets_{btc|evm|sol}
   - delete exactly this row from legacy table
4. Commit after each processed wallet (per-row atomic migration).

This matches "migrated wallet -> delete this wallet" behavior.
"""

from __future__ import annotations

import argparse
import importlib
import os
import re
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from typing import Iterable


@dataclass(frozen=True)
class LegacyRow:
    row_id: int
    created_at: str
    blockchain: str
    address: str
    mnemonic: str


def parse_env_file(path: str) -> dict[str, str]:
    env: dict[str, str] = {}
    if not os.path.isfile(path):
        return env

    with open(path, "r", encoding="utf-8") as handle:
        for raw_line in handle:
            line = raw_line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, value = line.split("=", 1)
            env[key.strip()] = value.strip().strip("\"'")
    return env


def resolve_connection(cli_conn: str | None, env_file: str) -> str:
    env_values = parse_env_file(env_file)
    conn = cli_conn or env_values.get("RECOVERY_POSTGRES_CONN") or os.environ.get("RECOVERY_POSTGRES_CONN")
    if not conn:
        raise ValueError(
            "PostgreSQL connection string is missing. Use --postgres-conn or set RECOVERY_POSTGRES_CONN in .env/environment."
        )
    return conn


def validate_table_name(name: str) -> str:
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name):
        raise ValueError(f"Invalid PostgreSQL table name: {name}")
    return name


def classify_chain(blockchain: str) -> str | None:
    chain = blockchain.strip().lower()
    if chain in {"btc", "bitcoin"}:
        return "btc"
    if chain in {"eth", "ethereum", "evm", "bsc", "polygon", "arbitrum", "optimism", "avalanche", "base"}:
        return "evm"
    if chain in {"sol", "solana"}:
        return "sol"
    return None


def require_module(name: str, install_hint: str):
    if importlib.util.find_spec(name) is None:
        raise RuntimeError(f"Module '{name}' is required. Install: {install_hint}")
    return importlib.import_module(name)


def connect_postgres(conn_str: str):
    psycopg_spec = importlib.util.find_spec("psycopg")
    if psycopg_spec is not None:
        psycopg = importlib.import_module("psycopg")
        return psycopg.connect(conn_str)

    psycopg2_spec = importlib.util.find_spec("psycopg2")
    if psycopg2_spec is not None:
        psycopg2 = importlib.import_module("psycopg2")
        return psycopg2.connect(conn_str)

    raise RuntimeError("PostgreSQL driver is missing. Install one of: python3 -m pip install psycopg OR psycopg2-binary")


def export_and_migrate(
    conn,
    *,
    legacy_table: str,
    excel_output: str,
    dry_run: bool,
) -> None:
    openpyxl = require_module("openpyxl", "python3 -m pip install openpyxl")

    wb = openpyxl.Workbook(write_only=True)
    sheets = {
        "all": wb.create_sheet("all"),
        "btc": wb.create_sheet("btc"),
        "evm": wb.create_sheet("evm"),
        "sol": wb.create_sheet("sol"),
        "unknown": wb.create_sheet("unknown"),
    }

    header = ["id", "created_at", "source_blockchain", "group_chain", "address", "mnemonic"]
    for sheet in sheets.values():
        sheet.append(header)

    read_cur = conn.cursor()
    read_cur.execute(f"SELECT id, created_at, blockchain, address, mnemonic FROM {legacy_table} ORDER BY id")

    transfer_sql = {
        "btc": (
            "INSERT INTO seed_phrases_btc (mnemonic) VALUES (%s) ON CONFLICT (mnemonic) DO NOTHING",
            "INSERT INTO recovered_wallets_btc (blockchain, address, mnemonic) VALUES (%s, %s, %s) ON CONFLICT (blockchain, address, mnemonic) DO NOTHING",
        ),
        "evm": (
            "INSERT INTO seed_phrases_evm (mnemonic) VALUES (%s) ON CONFLICT (mnemonic) DO NOTHING",
            "INSERT INTO recovered_wallets_evm (blockchain, address, mnemonic) VALUES (%s, %s, %s) ON CONFLICT (blockchain, address, mnemonic) DO NOTHING",
        ),
        "sol": (
            "INSERT INTO seed_phrases_sol (mnemonic) VALUES (%s) ON CONFLICT (mnemonic) DO NOTHING",
            "INSERT INTO recovered_wallets_sol (blockchain, address, mnemonic) VALUES (%s, %s, %s) ON CONFLICT (blockchain, address, mnemonic) DO NOTHING",
        ),
    }

    migrated = 0
    skipped_unknown = 0
    stats: dict[str, int] = {"btc": 0, "evm": 0, "sol": 0, "unknown": 0}

    for raw in read_cur:
        row = LegacyRow(
            row_id=int(raw[0]),
            created_at=str(raw[1]),
            blockchain=str(raw[2]).strip(),
            address=str(raw[3]).strip(),
            mnemonic=str(raw[4]).strip(),
        )

        chain_group = classify_chain(row.blockchain) or "unknown"
        stats[chain_group] += 1

        sheet_row = [row.row_id, row.created_at, row.blockchain, chain_group, row.address, row.mnemonic]
        sheets["all"].append(sheet_row)
        sheets[chain_group].append(sheet_row)

        if dry_run:
            continue

        if chain_group == "unknown":
            skipped_unknown += 1
            continue

        write_cur = conn.cursor()
        try:
            seed_sql, wallet_sql = transfer_sql[chain_group]
            write_cur.execute(seed_sql, (row.mnemonic,))
            write_cur.execute(wallet_sql, (row.blockchain, row.address, row.mnemonic))
            write_cur.execute(f"DELETE FROM {legacy_table} WHERE id = %s", (row.row_id,))
            conn.commit()
            migrated += 1
        except Exception:
            conn.rollback()
            raise
        finally:
            write_cur.close()

    read_cur.close()

    os.makedirs(os.path.dirname(excel_output) or ".", exist_ok=True)
    wb.save(excel_output)

    print(f"Exported rows to Excel: {excel_output}")
    print(f"Split stats => btc={stats['btc']} evm={stats['evm']} sol={stats['sol']} unknown={stats['unknown']}")

    if dry_run:
        print("Dry-run enabled: DB transfer and deletion skipped.")
        return

    print(f"Migrated+deleted wallets: {migrated}")
    if skipped_unknown:
        print(f"Skipped unknown wallets (not deleted): {skipped_unknown}")


def parse_args(argv: Iterable[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Migrate ALL rows from legacy recovered_wallets to split tables with XLSX export."
    )
    parser.add_argument("--env-file", default=".env", help="Path to .env file (default: .env)")
    parser.add_argument("--postgres-conn", default=None, help="PostgreSQL connection string")
    parser.add_argument("--legacy-table", default="recovered_wallets", help="Legacy source table")
    parser.add_argument(
        "--excel-output",
        default=f"legacy_wallets_export_{datetime.now(timezone.utc).strftime('%Y%m%d_%H%M%S')}.xlsx",
        help="Path to output XLSX file",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Only export XLSX and print stats; do not modify DB and do not delete rows.",
    )
    return parser.parse_args(argv)


def main(argv: Iterable[str] | None = None) -> int:
    args = parse_args(argv)

    try:
        conn_str = resolve_connection(args.postgres_conn, args.env_file)
        legacy_table = validate_table_name(args.legacy_table)

        conn = connect_postgres(conn_str)
        try:
            export_and_migrate(
                conn,
                legacy_table=legacy_table,
                excel_output=args.excel_output,
                dry_run=args.dry_run,
            )
        finally:
            conn.close()

        return 0
    except Exception as exc:  # noqa: BLE001
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

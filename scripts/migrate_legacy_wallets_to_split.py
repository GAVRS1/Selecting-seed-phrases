#!/usr/bin/env python3
"""Export rows from legacy recovered_wallets table to XLSX and then delete them.

Behavior:
1. Read rows from legacy table in batches (ordered by id).
2. Build an Excel workbook with one sheet per blockchain group.
3. If export succeeds, delete only the exported legacy rows from source table.

If export fails, no deletions are performed.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import tempfile
from collections import defaultdict
from dataclasses import dataclass
from datetime import datetime, timezone
from typing import Iterable

PSQL_BIN = os.environ.get("PSQL_BIN", "psql")


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


def run_psql(conn: str, sql: str) -> str:
    """Run SQL via a temp file to avoid Windows command-length limits."""
    with tempfile.NamedTemporaryFile("w", suffix=".sql", delete=False, encoding="utf-8") as handle:
        handle.write(sql)
        sql_path = handle.name

    cmd = [PSQL_BIN, conn, "-v", "ON_ERROR_STOP=1", "-At", "-F", "\t", "-f", sql_path]
    result = subprocess.run(cmd, capture_output=True, text=False, check=False)
    try:
        os.unlink(sql_path)
    except OSError:
        pass

def run_psql(conn: str, sql: str) -> str:
    """Run SQL via a temp file to avoid Windows command-length limits."""
    with tempfile.NamedTemporaryFile("w", suffix=".sql", delete=False, encoding="utf-8") as handle:
        handle.write(sql)
        sql_path = handle.name

    cmd = [PSQL_BIN, conn, "-v", "ON_ERROR_STOP=1", "-At", "-F", "\t", "-f", sql_path]
    result = subprocess.run(cmd, capture_output=True, text=True, check=False)
    try:
        os.unlink(sql_path)
    except OSError:
        pass

    if result.returncode != 0:
        raise RuntimeError(f"psql failed: {stderr.strip() or stdout.strip()}")
    return stdout


def fetch_legacy_rows(conn: str, legacy_table: str, batch_size: int, after_id: int = 0) -> list[LegacyRow]:
    where_clause = f"WHERE id > {after_id} " if after_id > 0 else ""
    sql = (
        f"SELECT id, created_at, blockchain, address, mnemonic FROM {legacy_table} "
        f"{where_clause}ORDER BY id LIMIT {batch_size};"
    )
    output = run_psql(conn, sql)

    rows: list[LegacyRow] = []
    for line in output.splitlines():
        if not line.strip():
            continue
        parts = line.split("\t")
        if len(parts) != 5:
            raise RuntimeError(f"Unexpected row format from psql: {line!r}")
        rows.append(
            LegacyRow(
                row_id=int(parts[0]),
                created_at=parts[1].strip(),
                blockchain=parts[2].strip(),
                address=parts[3].strip(),
                mnemonic=parts[4].strip(),
            )
        )
    return rows


def classify_chain(blockchain: str) -> str | None:
    chain = blockchain.strip().lower()
    if chain in {"btc", "bitcoin"}:
        return "btc"
    if chain in {"eth", "ethereum", "evm", "bsc", "polygon", "arbitrum", "optimism", "avalanche", "base"}:
        return "evm"
    if chain in {"sol", "solana"}:
        return "sol"
    return None


def export_xlsx(rows: list[LegacyRow], output_path: str) -> None:
    try:
        from openpyxl import Workbook
    except ImportError as exc:
        raise RuntimeError(
            "openpyxl is required for Excel export. Install it with: python3 -m pip install openpyxl"
        ) from exc

    wb = Workbook()
    default_sheet = wb.active
    wb.remove(default_sheet)

    groups: dict[str, list[LegacyRow]] = defaultdict(list)
    for row in rows:
        mapped = classify_chain(row.blockchain)
        sheet_key = mapped if mapped is not None else "unknown"
        groups[sheet_key].append(row)

    all_sheet = wb.create_sheet("all")
    header = ["id", "created_at", "source_blockchain", "group_chain", "address", "mnemonic"]
    all_sheet.append(header)
    for row in rows:
        all_sheet.append([
            row.row_id,
            row.created_at,
            row.blockchain,
            classify_chain(row.blockchain) or "unknown",
            row.address,
            row.mnemonic,
        ])

    for sheet_name in ("btc", "evm", "sol", "unknown"):
        sheet = wb.create_sheet(sheet_name)
        sheet.append(header)
        for row in groups.get(sheet_name, []):
            sheet.append([
                row.row_id,
                row.created_at,
                row.blockchain,
                classify_chain(row.blockchain) or "unknown",
                row.address,
                row.mnemonic,
            ])

    os.makedirs(os.path.dirname(output_path) or ".", exist_ok=True)
    wb.save(output_path)


def build_delete_sql(rows: list[LegacyRow], legacy_table: str) -> str:
    if not rows:
        return ""

    return f"""
BEGIN;
DELETE FROM {legacy_table}
WHERE id IN ({",".join(str(row.row_id) for row in rows)});
COMMIT;
"""


def parse_args(argv: Iterable[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Export legacy recovered_wallets rows to Excel and delete exported rows from source table."
    )
    parser.add_argument("--env-file", default=".env", help="Path to .env file (default: .env)")
    parser.add_argument("--postgres-conn", default=None, help="PostgreSQL connection string")
    parser.add_argument("--legacy-table", default="recovered_wallets", help="Legacy source table")
    parser.add_argument("--batch-size", type=int, default=1000, help="Rows to process in one run")
    parser.add_argument(
        "--excel-output",
        default=f"legacy_wallets_export_{datetime.now(timezone.utc).strftime('%Y%m%d_%H%M%S')}.xlsx",
        help="Path to output XLSX file",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Only export XLSX and print stats; do not delete rows from DB.",
    )
    return parser.parse_args(argv)


def main(argv: Iterable[str] | None = None) -> int:
    args = parse_args(argv)
    if args.batch_size <= 0:
        print("--batch-size must be > 0", file=sys.stderr)
        return 2

    try:
        conn = resolve_connection(args.postgres_conn, args.env_file)
        legacy_table = validate_table_name(args.legacy_table)
        first_batch = fetch_legacy_rows(conn, legacy_table, args.batch_size)
        if not first_batch:
            print("No rows found in legacy table; nothing to migrate.")
            return 0

        all_rows: list[LegacyRow] = []
        stats: dict[str, int] = {"btc": 0, "evm": 0, "sol": 0, "unknown": 0}
        total_processed = 0
        batch_index = 0
        next_after_id = 0

        while True:
            rows = first_batch if batch_index == 0 else fetch_legacy_rows(conn, legacy_table, args.batch_size, next_after_id)
            if not rows:
                break

            batch_index += 1
            all_rows.extend(rows)
            for row in rows:
                stats[classify_chain(row.blockchain) or "unknown"] += 1

            if not args.dry_run:
                transfer_sql = build_transfer_sql(rows, legacy_table)
                run_psql(conn, transfer_sql)
                total_processed += len(rows)
                print(
                    f"Batch {batch_index}: transferred/deleted {len(rows)} rows "
                    f"(ids {rows[0].row_id}..{rows[-1].row_id})"
                )
            next_after_id = rows[-1].row_id

        export_xlsx(all_rows, args.excel_output)
        print(f"Exported {len(all_rows)} rows to Excel: {args.excel_output}")
        print(f"Split stats => btc={stats['btc']} evm={stats['evm']} sol={stats['sol']} unknown={stats['unknown']}")

        if args.dry_run:
            print("Dry-run enabled: deletion skipped.")
            return 0

        print(f"Transferred and deleted rows from legacy table: {total_processed}")
        return 0
    except Exception as exc:  # noqa: BLE001
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

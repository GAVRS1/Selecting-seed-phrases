#!/usr/bin/env python3
"""Check balances for wallets stored in PostgreSQL and prune processed rows.

Workflow:
1. Read wallet records from PostgreSQL table (`blockchain`, `address`, `mnemonic`).
2. Query on-chain balances by blockchain.
3. If balance > 0, append `blockchain/address/mnemonic` to recovered_wallets.txt.
4. Delete processed wallet rows from PostgreSQL in both zero and non-zero balance cases.

Rows whose balances cannot be checked (API/network error) are not deleted.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from decimal import Decimal
from typing import Iterable

HTTP_TIMEOUT_SECONDS = 20
PSQL_BIN = os.environ.get("PSQL_BIN", "psql")


@dataclass(frozen=True)
class WalletRow:
    row_id: int
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


def resolve_config(cli_conn: str | None, cli_table: str, env_file: str) -> tuple[str, str]:
    env_values = parse_env_file(env_file)

    conn = cli_conn or env_values.get("RECOVERY_POSTGRES_CONN") or os.environ.get("RECOVERY_POSTGRES_CONN")
    table = cli_table or env_values.get("RECOVERY_POSTGRES_TABLE") or os.environ.get("RECOVERY_POSTGRES_TABLE") or "recovered_wallets"

    if not conn:
        raise ValueError(
            "PostgreSQL connection string is missing. Use --postgres-conn or set RECOVERY_POSTGRES_CONN in .env/environment."
        )

    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", table):
        raise ValueError(f"Invalid PostgreSQL table name: {table}")

    return conn, table


def run_psql(conn: str, sql: str) -> str:
    cmd = [PSQL_BIN, conn, "-v", "ON_ERROR_STOP=1", "-At", "-F", "\t", "-c", sql]
    result = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        raise RuntimeError(f"psql failed: {result.stderr.strip() or result.stdout.strip()}")
    return result.stdout


def fetch_rows(conn: str, table: str) -> list[WalletRow]:
    sql = f"SELECT id, blockchain, address, mnemonic FROM {table} ORDER BY id;"
    output = run_psql(conn, sql)

    rows: list[WalletRow] = []
    for line in output.splitlines():
        if not line.strip():
            continue
        parts = line.split("\t")
        if len(parts) != 4:
            raise RuntimeError(f"Unexpected row format from psql: {line!r}")

        rows.append(
            WalletRow(
                row_id=int(parts[0]),
                blockchain=parts[1].strip().lower(),
                address=parts[2].strip(),
                mnemonic=parts[3].strip(),
            )
        )
    return rows


def request_json(url: str, payload: dict | None = None) -> dict:
    data = None
    headers = {}
    if payload is not None:
        data = json.dumps(payload).encode("utf-8")
        headers["Content-Type"] = "application/json"

    req = urllib.request.Request(url=url, data=data, headers=headers, method="POST" if payload is not None else "GET")
    with urllib.request.urlopen(req, timeout=HTTP_TIMEOUT_SECONDS) as response:
        body = response.read().decode("utf-8", errors="replace")
    return json.loads(body)


def request_text(url: str) -> str:
    req = urllib.request.Request(url=url, method="GET")
    with urllib.request.urlopen(req, timeout=HTTP_TIMEOUT_SECONDS) as response:
        return response.read().decode("utf-8", errors="replace").strip()


def balance_btc(address: str) -> Decimal:
    satoshis = Decimal(request_text(f"https://blockchain.info/q/addressbalance/{address}"))
    return satoshis / Decimal("100000000")


def balance_eth(address: str) -> Decimal:
    payload = {
        "jsonrpc": "2.0",
        "method": "eth_getBalance",
        "params": [address, "latest"],
        "id": 1,
    }
    result = request_json("https://cloudflare-eth.com", payload)
    wei_hex = result.get("result")
    if not isinstance(wei_hex, str):
        raise RuntimeError(f"Unexpected ETH RPC response: {result}")
    wei = int(wei_hex, 16)
    return Decimal(wei) / Decimal("1000000000000000000")


def balance_sol(address: str) -> Decimal:
    payload = {
        "jsonrpc": "2.0",
        "id": 1,
        "method": "getBalance",
        "params": [address],
    }
    result = request_json("https://api.mainnet-beta.solana.com", payload)
    lamports = result.get("result", {}).get("value")
    if lamports is None:
        raise RuntimeError(f"Unexpected SOL RPC response: {result}")
    return Decimal(lamports) / Decimal("1000000000")


def balance_ton(address: str) -> Decimal:
    payload = request_json(f"https://toncenter.com/api/v2/getAddressBalance?address={address}")
    if not payload.get("ok"):
        raise RuntimeError(f"TON API error: {payload}")
    nanotons = payload.get("result")
    return Decimal(str(nanotons)) / Decimal("1000000000")


def fetch_balance(blockchain: str, address: str) -> Decimal:
    if blockchain == "btc":
        return balance_btc(address)
    if blockchain == "eth":
        return balance_eth(address)
    if blockchain == "sol":
        return balance_sol(address)
    if blockchain == "ton":
        return balance_ton(address)
    raise ValueError(f"Unsupported blockchain value: {blockchain}")


def append_recovered(path: str, wallet: WalletRow) -> None:
    with open(path, "a", encoding="utf-8") as handle:
        handle.write(f"{wallet.blockchain}/{wallet.address}/{wallet.mnemonic}\n")


def delete_rows(conn: str, table: str, row_ids: Iterable[int]) -> int:
    row_ids = list(row_ids)
    if not row_ids:
        return 0
    id_list = ",".join(str(i) for i in row_ids)
    sql = f"DELETE FROM {table} WHERE id IN ({id_list});"
    run_psql(conn, sql)
    return len(row_ids)


def process_wallets(conn: str, table: str, output_path: str, delay_seconds: float) -> int:
    wallets = fetch_rows(conn, table)
    print(f"Loaded wallets: {len(wallets)}")

    deleted_ids: list[int] = []
    recovered_count = 0

    for wallet in wallets:
        try:
            balance = fetch_balance(wallet.blockchain, wallet.address)
        except (urllib.error.URLError, TimeoutError, json.JSONDecodeError, ValueError, RuntimeError) as exc:
            print(f"[WARN] Skip id={wallet.row_id} {wallet.blockchain}:{wallet.address} due to: {exc}")
            continue

        print(f"id={wallet.row_id} chain={wallet.blockchain} address={wallet.address} balance={balance}")
        if balance > 0:
            append_recovered(output_path, wallet)
            recovered_count += 1

        deleted_ids.append(wallet.row_id)
        if delay_seconds > 0:
            time.sleep(delay_seconds)

    deleted_count = delete_rows(conn, table, deleted_ids)
    print(f"Recovered non-empty wallets: {recovered_count}")
    print(f"Deleted rows from DB: {deleted_count}")
    print(f"Skipped rows (not deleted): {len(wallets) - deleted_count}")

    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Read wallets from PostgreSQL, check balances, append non-empty wallets to file and delete processed rows."
    )
    parser.add_argument("--postgres-conn", help="PostgreSQL connection string. Defaults to RECOVERY_POSTGRES_CONN from .env/env")
    parser.add_argument("--postgres-table", default="", help="PostgreSQL table with wallet records (default: recovered_wallets)")
    parser.add_argument("--env-file", default=".env", help="Path to env file (default: .env)")
    parser.add_argument("--output", default="recovered_wallets.txt", help="Output file for non-empty wallets")
    parser.add_argument(
        "--delay-seconds",
        type=float,
        default=0.0,
        help="Optional delay between requests to reduce API rate-limit pressure",
    )
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    try:
        conn, table = resolve_config(args.postgres_conn, args.postgres_table, args.env_file)
        return process_wallets(conn, table, args.output, args.delay_seconds)
    except Exception as exc:  # noqa: BLE001 - command-line entrypoint
        print(f"[ERROR] {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

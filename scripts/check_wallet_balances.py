#!/usr/bin/env python3
"""Check balances for wallets stored in PostgreSQL or manual TXT file.

Workflow:
1. Read wallet records from PostgreSQL table (`blockchain`, `address`, `mnemonic`)
   or from `manual_wallets.txt`.
2. Query on-chain balances by blockchain.
3. Print `blockchain/address/mnemonic/balance` to console for each successfully checked wallet.
4. If balance/assets > 0, append `blockchain/address/mnemonic_or_test/balance` to recovered_wallets.txt.
5. In PostgreSQL mode, delete processed wallet rows from DB in both zero and non-zero balance cases.

Rows whose balances cannot be checked (API/network error) are not deleted in PostgreSQL mode.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import time
import socket
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from decimal import Decimal
from typing import Iterable

HTTP_TIMEOUT_SECONDS = 20
PSQL_BIN = os.environ.get("PSQL_BIN", "psql")
DEFAULT_PROXY_ENABLED = False
PROXY_FILE_PATH = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "data", "proxies.txt"))
MAX_PROXY_ATTEMPTS = 3
DEFAULT_ETH_RPC_URL = "https://ethereum-rpc.publicnode.com"

NETWORK_OPENER: urllib.request.OpenerDirector | None = None
PROXY_ROTATOR: "ProxyRotator | None" = None
ETH_RPC_URL: str = DEFAULT_ETH_RPC_URL


@dataclass(frozen=True)
class WalletRow:
    row_id: int | None
    blockchain: str
    address: str
    mnemonic: str


@dataclass(frozen=True)
class BalanceCheckResult:
    amount: Decimal
    display: str
    has_assets: bool


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


def parse_manual_wallets(path: str) -> list[WalletRow]:
    wallets: list[WalletRow] = []
    with open(path, "r", encoding="utf-8") as handle:
        for line_no, raw_line in enumerate(handle, start=1):
            line = raw_line.strip()
            if not line or line.startswith("#"):
                continue

            normalized = line.replace(";", ",")
            if "," in normalized:
                parts = [p.strip() for p in normalized.split(",", 1)]
            else:
                parts = normalized.split(None, 1)

            if len(parts) != 2:
                raise ValueError(
                    f"Invalid manual wallet format at {path}:{line_no}. "
                    "Expected 'chain,address' (also supports ';' or single space)."
                )

            blockchain, address = parts[0].lower(), parts[1]
            wallets.append(WalletRow(row_id=None, blockchain=blockchain, address=address, mnemonic=""))
    return wallets


def request_json(url: str, payload: dict | None = None) -> dict:
    data = None
    headers = {}
    if payload is not None:
        data = json.dumps(payload).encode("utf-8")
        headers["Content-Type"] = "application/json"

    req = urllib.request.Request(url=url, data=data, headers=headers, method="POST" if payload is not None else "GET")
    with open_url(req) as response:
        body = response.read().decode("utf-8", errors="replace")
    return json.loads(body)


def request_text(url: str) -> str:
    req = urllib.request.Request(url=url, method="GET")
    with open_url(req) as response:
        return response.read().decode("utf-8", errors="replace").strip()


def parse_bool_env(value: str | None, *, default: bool = False) -> bool:
    if value is None:
        return default
    normalized = value.strip().lower()
    if normalized in {"1", "true", "yes", "on"}:
        return True
    if normalized in {"0", "false", "no", "off"}:
        return False
    raise ValueError(f"Invalid boolean value: {value!r}")


def build_proxy_url(proxy_raw: str) -> str:
    proxy_parts = proxy_raw.strip().split(":")
    if len(proxy_parts) not in (2, 4):
        raise ValueError(
            "Invalid RECOVERY_BALANCE_CHECKER_PROXY format. "
            "Use host:port or host:port:username:password."
        )

    host, port = proxy_parts[0].strip(), proxy_parts[1].strip()
    if not host or not port:
        raise ValueError("Invalid RECOVERY_BALANCE_CHECKER_PROXY: host and port must be non-empty.")

    if len(proxy_parts) == 2:
        return f"http://{host}:{port}"

    username = urllib.parse.quote(proxy_parts[2], safe="")
    password = urllib.parse.quote(proxy_parts[3], safe="")
    return f"http://{username}:{password}@{host}:{port}"


def load_proxy_urls(path: str) -> list[str]:
    if not os.path.isfile(path):
        raise ValueError(f"Proxy file not found: {path}")

    proxies: list[str] = []
    with open(path, "r", encoding="utf-8") as handle:
        for line_no, raw_line in enumerate(handle, start=1):
            line = raw_line.strip()
            if not line or line.startswith("#"):
                continue
            try:
                proxies.append(build_proxy_url(line))
            except ValueError as exc:
                raise ValueError(f"Invalid proxy at {path}:{line_no}: {exc}") from exc

    if not proxies:
        raise ValueError(f"Proxy file is empty: {path}")
    return proxies


class ProxyRotator:
    def __init__(self, proxies: list[str]) -> None:
        self._proxies = proxies
        self._index = 0
        self._current_opener = self._make_opener(self._proxies[self._index])

    @staticmethod
    def _make_opener(proxy_url: str) -> urllib.request.OpenerDirector:
        handler = urllib.request.ProxyHandler({"http": proxy_url, "https": proxy_url})
        return urllib.request.build_opener(handler)

    def current_opener(self) -> urllib.request.OpenerDirector:
        return self._current_opener

    def current_proxy(self) -> str:
        return self._proxies[self._index]

    def rotate(self) -> str:
        self._index = (self._index + 1) % len(self._proxies)
        self._current_opener = self._make_opener(self._proxies[self._index])
        return self.current_proxy()

    def size(self) -> int:
        return len(self._proxies)


def should_rotate_proxy(exc: Exception) -> bool:
    if isinstance(exc, (socket.timeout, TimeoutError)):
        return True
    if isinstance(exc, urllib.error.HTTPError):
        return exc.code >= 500 or exc.code == 429
    if isinstance(exc, urllib.error.URLError):
        return True
    return False


def open_url(req: urllib.request.Request):
    global NETWORK_OPENER, PROXY_ROTATOR  # noqa: PLW0603
    attempts = 1
    if PROXY_ROTATOR is not None:
        attempts = min(PROXY_ROTATOR.size(), MAX_PROXY_ATTEMPTS)

    last_exc: Exception | None = None
    for attempt in range(1, attempts + 1):
        try:
            if NETWORK_OPENER is not None:
                return NETWORK_OPENER.open(req, timeout=HTTP_TIMEOUT_SECONDS)
            return urllib.request.urlopen(req, timeout=HTTP_TIMEOUT_SECONDS)
        except Exception as exc:  # noqa: BLE001 - normalized retry for network path
            last_exc = exc
            if PROXY_ROTATOR is None or not should_rotate_proxy(exc) or attempt == attempts:
                raise
            next_proxy = PROXY_ROTATOR.rotate()
            NETWORK_OPENER = PROXY_ROTATOR.current_opener()
            print(f"[WARN] Request failed via proxy, switching to next proxy: {next_proxy}")

    if last_exc is not None:
        raise last_exc
    raise RuntimeError("Unexpected network request state")


def configure_network(env_file: str) -> None:
    global NETWORK_OPENER, PROXY_ROTATOR, ETH_RPC_URL  # noqa: PLW0603
    env_values = parse_env_file(env_file)
    ETH_RPC_URL = (
        os.environ.get("RECOVERY_ETH_RPC_URL")
        or env_values.get("RECOVERY_ETH_RPC_URL")
        or DEFAULT_ETH_RPC_URL
    )
    proxy_enabled = parse_bool_env(
        os.environ.get("RECOVERY_BALANCE_CHECKER_PROXY_ENABLED")
        or env_values.get("RECOVERY_BALANCE_CHECKER_PROXY_ENABLED"),
        default=DEFAULT_PROXY_ENABLED,
    )
    if not proxy_enabled:
        return

    proxy_urls = load_proxy_urls(PROXY_FILE_PATH)
    PROXY_ROTATOR = ProxyRotator(proxy_urls)
    NETWORK_OPENER = PROXY_ROTATOR.current_opener()
    print(f"[INFO] Proxy is enabled for balance checker requests. Loaded proxies: {len(proxy_urls)}")


def format_decimal(value: Decimal, *, precision: int = 18) -> str:
    quantized = value.quantize(Decimal(1).scaleb(-precision)).normalize()
    return format(quantized, "f")


def balance_btc(address: str) -> Decimal:
    payload = request_json(f"https://blockstream.info/api/address/{address}")
    chain = payload.get("chain_stats", {})
    mempool = payload.get("mempool_stats", {})

    funded = int(chain.get("funded_txo_sum", 0)) + int(mempool.get("funded_txo_sum", 0))
    spent = int(chain.get("spent_txo_sum", 0)) + int(mempool.get("spent_txo_sum", 0))
    satoshis = funded - spent
    return Decimal(satoshis) / Decimal("100000000")


def balance_eth(address: str) -> tuple[Decimal, str, bool]:
    payload = {
        "jsonrpc": "2.0",
        "method": "eth_getBalance",
        "params": [address, "latest"],
        "id": 1,
    }
    result = request_json(ETH_RPC_URL, payload)
    wei_hex = result.get("result")
    if not isinstance(wei_hex, str):
        raise RuntimeError(f"Unexpected ETH RPC response: {result}")
    wei = int(wei_hex, 16)
    native_eth = Decimal(wei) / Decimal("1000000000000000000")

    # Also check ERC-20 token balances via Ethplorer public endpoint
    # so non-ETH token holdings are not missed.
    token_info = request_json(
        f"https://api.ethplorer.io/getAddressInfo/{urllib.parse.quote(address)}?apiKey=freekey"
    )
    token_chunks: list[str] = []
    for token in token_info.get("tokens", []) or []:
        token_balance_raw = token.get("balance")
        token_meta = token.get("tokenInfo", {})
        if token_balance_raw in (None, ""):
            continue
        decimals_raw = token_meta.get("decimals")
        symbol = token_meta.get("symbol") or token_meta.get("name") or "TOKEN"
        try:
            decimals = int(decimals_raw) if decimals_raw not in (None, "") else 0
            token_amount = Decimal(str(token_balance_raw)) / (Decimal(10) ** decimals)
        except Exception:
            continue
        if token_amount > 0:
            token_chunks.append(f"{symbol}:{format_decimal(token_amount, precision=8)}")

    eth_display = f"ETH:{format_decimal(native_eth, precision=8)}"
    has_token_assets = bool(token_chunks)
    if has_token_assets:
        eth_display += " | TOKENS:" + ",".join(token_chunks)
    return native_eth, eth_display, native_eth > 0 or has_token_assets


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


def fetch_balance(blockchain: str, address: str) -> BalanceCheckResult:
    if blockchain == "btc":
        amount = balance_btc(address)
        return BalanceCheckResult(amount=amount, display=format_decimal(amount, precision=8), has_assets=amount > 0)
    if blockchain == "eth":
        amount, display, has_assets = balance_eth(address)
        return BalanceCheckResult(amount=amount, display=display, has_assets=has_assets)
    if blockchain == "sol":
        amount = balance_sol(address)
        return BalanceCheckResult(amount=amount, display=format_decimal(amount, precision=9), has_assets=amount > 0)
    if blockchain == "ton":
        amount = balance_ton(address)
        return BalanceCheckResult(amount=amount, display=format_decimal(amount, precision=9), has_assets=amount > 0)
    raise ValueError(f"Unsupported blockchain value: {blockchain}")


def append_recovered(path: str, wallet: WalletRow, balance_text: str) -> None:
    mnemonic = wallet.mnemonic if wallet.mnemonic else "test"
    with open(path, "a", encoding="utf-8") as handle:
        handle.write(f"{wallet.blockchain}/{wallet.address}/{mnemonic}/{balance_text}\n")


def delete_rows(conn: str, table: str, row_ids: Iterable[int]) -> int:
    row_ids = list(row_ids)
    if not row_ids:
        return 0
    id_list = ",".join(str(i) for i in row_ids)
    sql = f"DELETE FROM {table} WHERE id IN ({id_list});"
    run_psql(conn, sql)
    return len(row_ids)


def process_wallets(
    wallets: list[WalletRow],
    output_path: str,
    delay_seconds: float,
    *,
    conn: str | None = None,
    table: str | None = None,
) -> int:
    db_mode = conn is not None and table is not None
    print(f"Loaded wallets: {len(wallets)}")

    recovered_count = 0
    deleted_count = 0

    for wallet in wallets:
        try:
            check = fetch_balance(wallet.blockchain, wallet.address)
        except (urllib.error.URLError, TimeoutError, json.JSONDecodeError, ValueError, RuntimeError) as exc:
            wallet_ref = f"id={wallet.row_id} " if wallet.row_id is not None else ""
            print(f"[WARN] Skip {wallet_ref}{wallet.blockchain}:{wallet.address} due to: {exc}")
            continue

        mnemonic = wallet.mnemonic if wallet.mnemonic else "test"
        print(f"{wallet.blockchain}/{wallet.address}/{mnemonic}/{check.display}")
        if check.has_assets:
            append_recovered(output_path, wallet, check.display)
            recovered_count += 1
            wallet_ref = f"id={wallet.row_id} " if wallet.row_id is not None else ""
            print(f"[WRITE] {wallet_ref}-> {output_path}")
        else:
            if wallet.row_id is not None:
                print(f"[DELETE] id={wallet.row_id} zero balance")
            else:
                print("[SKIP] zero balance")

        if db_mode and wallet.row_id is not None:
            delete_rows(conn, table, [wallet.row_id])
            deleted_count += 1
        if delay_seconds > 0:
            time.sleep(delay_seconds)

    print(f"Recovered non-empty wallets: {recovered_count}")
    if db_mode:
        print(f"Deleted rows from DB: {deleted_count}")
        print(f"Skipped rows (not deleted): {len(wallets) - deleted_count}")
    else:
        print("Manual TXT mode: DB delete step skipped")

    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Read wallets from PostgreSQL or manual TXT, check balances, append non-empty wallets to output file. "
            "In PostgreSQL mode processed rows are deleted."
        )
    )
    parser.add_argument(
        "--manual-wallets",
        default="",
        help="Path to manual wallets TXT file (chain,address per line). If set, PostgreSQL is not used.",
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
        configure_network(args.env_file)
        print(f"[INFO] ETH RPC endpoint: {ETH_RPC_URL}")
        if args.manual_wallets:
            wallets = parse_manual_wallets(args.manual_wallets)
            return process_wallets(wallets, args.output, args.delay_seconds)

        conn, table = resolve_config(args.postgres_conn, args.postgres_table, args.env_file)
        wallets = fetch_rows(conn, table)
        return process_wallets(wallets, args.output, args.delay_seconds, conn=conn, table=table)
    except Exception as exc:  # noqa: BLE001 - command-line entrypoint
        print(f"[ERROR] {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

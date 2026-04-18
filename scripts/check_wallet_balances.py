#!/usr/bin/env python3
"""Check balances for wallets stored in PostgreSQL.

Workflow:
1. Read wallet records from PostgreSQL result tables (`btc`).
2. Query on-chain balances by blockchain.
3. Print `blockchain/address/mnemonic/balance` to console for each successfully checked wallet.
4. If balance/assets > 0, append `blockchain/address/mnemonic_or_test/balance` to recovered_wallets.txt.
5. In PostgreSQL mode, delete processed wallet rows from DB in both zero and non-zero balance cases.

Rows whose balances cannot be checked (API/network error) are not deleted in PostgreSQL mode.
"""

from __future__ import annotations

import argparse
import hashlib
import http.client
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
EVM_RPC_FILE_PATH = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "data", "evm_rpc_urls.txt"))
EVM_TOKEN_CONTRACTS_FILE_PATH = os.path.normpath(
    os.path.join(os.path.dirname(__file__), "..", "data", "evm_token_contracts.txt")
)
MAX_PROXY_ATTEMPTS = 3
DEFAULT_ETH_RPC_URL = "https://ethereum-rpc.publicnode.com"
DEFAULT_DELAY_SECONDS = 0.2
SUPPORTED_RESULT_CHAINS = ("btc",)
DEFAULT_RESULT_TABLE_BY_CHAIN = {
    "btc": "recovered_wallets_btc",
}

NETWORK_OPENER: urllib.request.OpenerDirector | None = None
PROXY_ROTATOR: "ProxyRotator | None" = None
ETH_RPC_URL: str = DEFAULT_ETH_RPC_URL
EVM_RPC_URLS: list[str] = [DEFAULT_ETH_RPC_URL]
EVM_TOKEN_CONTRACTS: list[str] = []


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


def resolve_connection(cli_conn: str | None, env_file: str) -> str:
    env_values = parse_env_file(env_file)

    conn = cli_conn or env_values.get("RECOVERY_POSTGRES_CONN") or os.environ.get("RECOVERY_POSTGRES_CONN")

    if not conn:
        raise ValueError(
            "PostgreSQL connection string is missing. Use --postgres-conn or set RECOVERY_POSTGRES_CONN in .env/environment."
        )
    return conn


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


def resolve_result_tables(
    env_file: str,
    *,
    cli_table_btc: str,
) -> dict[str, str]:
    env_values = parse_env_file(env_file)
    table_btc = cli_table_btc or env_values.get("RECOVERY_POSTGRES_RESULT_TABLE_BTC") or os.environ.get("RECOVERY_POSTGRES_RESULT_TABLE_BTC") or DEFAULT_RESULT_TABLE_BY_CHAIN["btc"]
    by_chain = {"btc": table_btc}
    for chain, table in by_chain.items():
        if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", table):
            raise ValueError(f"Invalid PostgreSQL table name for {chain}: {table}")
    return by_chain


def request_json(url: str, payload: dict | None = None) -> dict:
    data = None
    headers = {
        "Accept": "application/json",
        "User-Agent": "curl/8.0.1",
    }
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


def parse_non_negative_float(value: str | None, *, default: float) -> float:
    if value is None:
        return default
    try:
        parsed = float(value.strip())
    except ValueError as exc:
        raise ValueError(f"Invalid float value: {value!r}") from exc
    if parsed < 0:
        raise ValueError(f"Float value must be >= 0: {value!r}")
    return parsed


def parse_csv_env(value: str | None) -> list[str]:
    if value is None:
        return []
    chunks: list[str] = []
    normalized = value.replace("\n", ",").replace(";", ",")
    for chunk in normalized.split(","):
        token = chunk.strip()
        if token:
            chunks.append(token)
    return chunks


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
    if isinstance(exc, http.client.RemoteDisconnected):
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
    global NETWORK_OPENER, PROXY_ROTATOR, ETH_RPC_URL, EVM_RPC_URLS, EVM_TOKEN_CONTRACTS  # noqa: PLW0603
    env_values = parse_env_file(env_file)
    ETH_RPC_URL = (
        os.environ.get("RECOVERY_ETH_RPC_URL")
        or env_values.get("RECOVERY_ETH_RPC_URL")
        or DEFAULT_ETH_RPC_URL
    )
    EVM_RPC_URLS = parse_csv_env(
        os.environ.get("RECOVERY_EVM_RPC_URLS")
        or env_values.get("RECOVERY_EVM_RPC_URLS")
    )
    if not EVM_RPC_URLS:
        EVM_RPC_URLS = [ETH_RPC_URL]
    EVM_TOKEN_CONTRACTS = parse_csv_env(
        os.environ.get("RECOVERY_EVM_TOKEN_CONTRACTS")
        or env_values.get("RECOVERY_EVM_TOKEN_CONTRACTS")
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


def normalize_hex_address(address: str) -> str:
    normalized = address.strip().lower()
    if normalized.startswith("0x"):
        normalized = normalized[2:]
    if len(normalized) != 40 or not re.fullmatch(r"[0-9a-f]{40}", normalized):
        raise ValueError(f"Invalid EVM address: {address}")
    return f"0x{normalized}"


def evm_rpc_urls_for_wallet(address: str) -> list[str]:
    if not EVM_RPC_URLS:
        return [ETH_RPC_URL]
    digest = hashlib.sha256(address.encode("utf-8")).digest()
    start = int.from_bytes(digest[:4], byteorder="big") % len(EVM_RPC_URLS)
    return EVM_RPC_URLS[start:] + EVM_RPC_URLS[:start]


def evm_rpc_request(address: str, method: str, params: list, *, allow_method_fallback: bool = False) -> dict:
    last_error: Exception | None = None
    for rpc_url in evm_rpc_urls_for_wallet(address):
        payload = {"jsonrpc": "2.0", "method": method, "params": params, "id": 1}
        try:
            response = request_json(rpc_url, payload)
        except Exception as exc:  # noqa: BLE001 - failover between independent RPC providers
            last_error = exc
            continue

        if "error" in response:
            code = response.get("error", {}).get("code")
            if allow_method_fallback and code in (-32601, -32602):
                return response
            last_error = RuntimeError(f"RPC {rpc_url} returned error for {method}: {response['error']}")
            continue
        return response

    if last_error is not None:
        raise last_error
    raise RuntimeError(f"Cannot execute RPC request method={method}")


def evm_eth_call(address: str, to: str, data: str) -> str:
    result = evm_rpc_request(
        address,
        "eth_call",
        [{"to": to, "data": data}, "latest"],
    ).get("result")
    if not isinstance(result, str):
        raise RuntimeError(f"Unexpected eth_call result for {to}: {result!r}")
    return result


def decode_symbol(symbol_hex: str) -> str:
    raw = symbol_hex[2:] if symbol_hex.startswith("0x") else symbol_hex
    if not raw:
        return "TOKEN"
    try:
        data = bytes.fromhex(raw)
    except ValueError:
        return "TOKEN"

    # dynamic string ABI
    if len(data) >= 96:
        offset = int.from_bytes(data[:32], "big")
        if offset + 32 <= len(data):
            length = int.from_bytes(data[offset : offset + 32], "big")
            start = offset + 32
            end = start + length
            if end <= len(data):
                decoded = data[start:end].decode("utf-8", errors="ignore").strip("\x00").strip()
                if decoded:
                    return decoded

    # bytes32 symbol fallback
    decoded = data[:32].decode("utf-8", errors="ignore").strip("\x00").strip()
    return decoded or "TOKEN"


def fetch_evm_token_meta(wallet_address: str, token_address: str) -> tuple[int, str]:
    decimals = 0
    symbol = "TOKEN"
    try:
        decimals_hex = evm_eth_call(wallet_address, token_address, "0x313ce567")
        decimals = int(decimals_hex, 16)
    except Exception:
        decimals = 0
    try:
        symbol_hex = evm_eth_call(wallet_address, token_address, "0x95d89b41")
        symbol = decode_symbol(symbol_hex)
    except Exception:
        symbol = "TOKEN"
    return decimals, symbol


def read_evm_token_balance(wallet_address: str, token_address: str) -> tuple[Decimal, str]:
    token = normalize_hex_address(token_address)
    owner = normalize_hex_address(wallet_address)[2:]
    balance_of_data = "0x70a08231" + ("0" * 24) + owner
    balance_hex = evm_eth_call(wallet_address, token, balance_of_data)
    raw_balance = int(balance_hex, 16)
    if raw_balance == 0:
        return Decimal(0), "TOKEN"
    decimals, symbol = fetch_evm_token_meta(wallet_address, token)
    amount = Decimal(raw_balance) / (Decimal(10) ** decimals if decimals >= 0 else Decimal(1))
    return amount, symbol


def discover_tokens_via_alchemy(wallet_address: str) -> list[tuple[str, Decimal, str]]:
    response = evm_rpc_request(
        wallet_address,
        "alchemy_getTokenBalances",
        [normalize_hex_address(wallet_address), "erc20"],
        allow_method_fallback=True,
    )
    if "error" in response:
        return []
    result = response.get("result", {})
    token_rows = result.get("tokenBalances", [])
    discovered: list[tuple[str, Decimal, str]] = []
    for token_row in token_rows:
        contract = token_row.get("contractAddress")
        token_balance_hex = token_row.get("tokenBalance")
        if not isinstance(contract, str) or not isinstance(token_balance_hex, str):
            continue
        raw_balance = int(token_balance_hex, 16)
        if raw_balance <= 0:
            continue
        token = normalize_hex_address(contract)
        decimals, symbol = fetch_evm_token_meta(wallet_address, token)
        amount = Decimal(raw_balance) / (Decimal(10) ** decimals if decimals >= 0 else Decimal(1))
        if amount > 0:
            discovered.append((token, amount, symbol))
    return discovered


def balance_btc(address: str) -> Decimal:
    payload = request_json(f"https://blockstream.info/api/address/{address}")
    chain = payload.get("chain_stats", {})
    mempool = payload.get("mempool_stats", {})

    funded = int(chain.get("funded_txo_sum", 0)) + int(mempool.get("funded_txo_sum", 0))
    spent = int(chain.get("spent_txo_sum", 0)) + int(mempool.get("spent_txo_sum", 0))
    satoshis = funded - spent
    return Decimal(satoshis) / Decimal("100000000")


def balance_eth(address: str) -> tuple[Decimal, str, bool]:
    result = evm_rpc_request(address, "eth_getBalance", [normalize_hex_address(address), "latest"])
    wei_hex = result.get("result")
    if not isinstance(wei_hex, str):
        raise RuntimeError(f"Unexpected ETH RPC response: {result}")
    wei = int(wei_hex, 16)
    native_eth = Decimal(wei) / Decimal("1000000000000000000")
    token_chunks: list[str] = []
    seen_contracts: set[str] = set()

    for token_contract, token_amount, symbol in discover_tokens_via_alchemy(address):
        seen_contracts.add(token_contract.lower())
        token_chunks.append(f"{symbol}:{format_decimal(token_amount, precision=8)}")

    for token_contract in EVM_TOKEN_CONTRACTS:
        try:
            normalized_contract = normalize_hex_address(token_contract)
        except ValueError as exc:
            print(f"[WARN] Skip invalid token contract in RECOVERY_EVM_TOKEN_CONTRACTS: {token_contract} ({exc})")
            continue
        if normalized_contract.lower() in seen_contracts:
            continue
        token_amount, symbol = read_evm_token_balance(address, normalized_contract)
        if token_amount > 0:
            token_chunks.append(f"{symbol}:{format_decimal(token_amount, precision=8)}")

    eth_display = f"ETH:{format_decimal(native_eth, precision=8)}"
    has_token_assets = bool(token_chunks)
    if has_token_assets:
        eth_display += " | TOKENS:" + ",".join(token_chunks)
    return native_eth, eth_display, native_eth > 0 or has_token_assets


def fetch_balance(blockchain: str, address: str) -> BalanceCheckResult:
    if blockchain == "btc":
        amount = balance_btc(address)
        return BalanceCheckResult(amount=amount, display=format_decimal(amount, precision=8), has_assets=amount > 0)
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
        except (
            urllib.error.URLError,
            TimeoutError,
            http.client.RemoteDisconnected,
            json.JSONDecodeError,
            ValueError,
            RuntimeError,
        ) as exc:
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


def process_result_chain(
    chain: str,
    conn: str,
    table: str,
    output_path: str,
    delay_seconds: float,
) -> int:
    print("=" * 72)
    print(f"[{chain.upper()} CONSOLE] table={table}")
    wallets = fetch_rows(conn, table)
    wallets = [wallet for wallet in wallets if wallet.blockchain == chain]
    return process_wallets(wallets, output_path, delay_seconds, conn=conn, table=table)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Read wallets from PostgreSQL, check balances, append non-empty wallets to output file, "
            "then delete processed rows."
        )
    )
    parser.add_argument("--postgres-conn", help="PostgreSQL connection string. Defaults to RECOVERY_POSTGRES_CONN from .env/env")
    parser.add_argument("--postgres-table-btc", default="", help="PostgreSQL BTC result table (default: recovered_wallets_btc)")
    parser.add_argument(
        "--chain",
        choices=SUPPORTED_RESULT_CHAINS,
        default="",
        help="Check only one chain console (btc). By default checks BTC table.",
    )
    parser.add_argument("--env-file", default=".env", help="Path to env file (default: .env)")
    parser.add_argument("--output", default="recovered_wallets.txt", help="Output file for non-empty wallets")
    parser.add_argument(
        "--delay-seconds",
        type=float,
        default=None,
        help=(
            "Optional delay between wallet checks to reduce API rate-limit pressure. "
            "Defaults to RECOVERY_BALANCE_CHECKER_DELAY_SECONDS or 0.2."
        ),
    )
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    try:
        configure_network(args.env_file)
        env_values = parse_env_file(args.env_file)
        delay_seconds = (
            args.delay_seconds
            if args.delay_seconds is not None
            else parse_non_negative_float(
                os.environ.get("RECOVERY_BALANCE_CHECKER_DELAY_SECONDS")
                or env_values.get("RECOVERY_BALANCE_CHECKER_DELAY_SECONDS"),
                default=DEFAULT_DELAY_SECONDS,
            )
        )
        if delay_seconds < 0:
            raise ValueError("--delay-seconds must be >= 0")

        print(f"[INFO] EVM RPC endpoints loaded: {len(EVM_RPC_URLS)}")
        if EVM_TOKEN_CONTRACTS:
            print(f"[INFO] Extra EVM token contracts configured: {len(EVM_TOKEN_CONTRACTS)}")
        print(f"[INFO] Checker delay between wallets: {delay_seconds} sec")
        conn = resolve_connection(args.postgres_conn, args.env_file)

        tables_by_chain = resolve_result_tables(
            args.env_file,
            cli_table_btc=args.postgres_table_btc,
        )
        if args.chain:
            return process_result_chain(args.chain, conn, tables_by_chain[args.chain], args.output, delay_seconds)

        for chain in SUPPORTED_RESULT_CHAINS:
            process_result_chain(chain, conn, tables_by_chain[chain], args.output, delay_seconds)
        return 0
    except Exception as exc:  # noqa: BLE001 - command-line entrypoint
        print(f"[ERROR] {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

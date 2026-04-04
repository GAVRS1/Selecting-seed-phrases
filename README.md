# Wallet Recovery Tool Skeleton (Legal Self-Recovery Only)

This repository now contains a **C++20 project scaffold** for a legal wallet recovery workflow based on partially known BIP-39 mnemonics.

## Important notice

- This tool is intended **only** for recovering wallets that belong to you.
- Do not use this software for unauthorized access attempts.
- Run in an offline environment when handling secrets.
- Do not log mnemonic phrases, seeds, or private keys.

## Implemented scaffold

- `bip39::Wordlist` for wordlist loading and lookup.
- `bip39::MnemonicValidator` with structure-level checks and a placeholder checksum routine.
- `bip39::MnemonicGenerator` for wildcard expansion and candidate filtering.
- `chains::IChainModule` and per-chain modules (`btc`, `eth`, `sol`).
- Real seed derivation via BIP-39 PBKDF2-HMAC-SHA512 and chain-specific key trees (BIP-32 for BTC/ETH, SLIP-0010 Ed25519 for SOL).
- Ethereum addresses are intentionally prefixed with `edu_eth_` as an explicit training marker.
- `engine::Pipeline` and `engine::Matcher` for async candidate processing and address matching.
- Per-chain deduplication: once a chain wallet is recovered, that chain is skipped for all next candidates.
- Scanner-based balance checks for derived BTC/ETH/SOL addresses.
- Console output in `coin` units (not USD), e.g. `0 coin`, `0.15 coin`.
- Automatic persistence of recovered wallets (`chain`, `address`, `balance_coin`, `mnemonic`) to a TXT file.
- Basic CLI parser and executable entrypoint.
- Minimal tests (`test_bip39`, `test_derivation`, `test_pipeline`).

## Build

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Run (example)

```bash
./build/recovery_tool \
  --template "abandon,ability,*,*,abandon,ability,abandon,ability,abandon,ability,abandon,ability" \
  --recovered-wallets ./recovered_wallets.txt \
  --bip39-passphrase "" \
  --paths-btc "m/84'/0'/0'/0/{i}" \
  --paths-eth "m/44'/60'/0'/0/{i}" \
  --paths-sol "m/44'/501'/{i}'/0'" \
  --scan-limit 20 \
  --max-candidates 100000 \
  --threads 8
```

> Notes:
> - `--target-addresses` is now optional.
> - For ETH scanner requests set `ETHERSCAN_API_KEY` in the environment for stable results.

## Быстрый запуск в Windows (.bat)

1. Установите **CMake** и **компилятор C++** (например, Visual Studio Build Tools с MSVC).
2. В корне репозитория запустите `run_project.bat` двойным кликом.
3. При необходимости отредактируйте переменную `TEMPLATE` внутри `run_project.bat` под вашу seed-фразу (через запятую, `*` для неизвестных слов).

Скрипт автоматически:
- настраивает CMake в `build/` (если папки ещё нет),
- собирает проект,
- запускает `build\recovery_tool.exe` с базовыми параметрами.

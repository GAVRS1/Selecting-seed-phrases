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
- Ethereum addresses are output in canonical hex format with `0x` prefix.
- `engine::Pipeline` and `engine::Matcher` for async candidate processing and address matching.
- Per-chain deduplication: once a chain wallet is recovered, that chain is skipped for all next candidates.
- Scanner-based balance checks for derived BTC/ETH/SOL addresses.
- Manual wallet balance-check mode from a TXT file (`--manual-wallets`) for parser/scanner verification.
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



### Windows: OpenSSL dependency

This project links against `OpenSSL::Crypto`, so OpenSSL development files must be available at configure time.

Recommended (vcpkg):

```powershell
# one-time
git clone https://github.com/microsoft/vcpkg $env:USERPROFILE\vcpkg
& $env:USERPROFILE\vcpkg\bootstrap-vcpkg.bat
setx VCPKG_ROOT "$env:USERPROFILE\vcpkg"
& $env:USERPROFILE\vcpkg\vcpkg install openssl:x64-windows

# open a NEW terminal after setx, then configure this project
cmake -S . -B build `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build --config Release
```

Quick troubleshooting (PowerShell):

```powershell
# verify env var
$env:VCPKG_ROOT

# verify package installed
& $env:VCPKG_ROOT\vcpkg list | Select-String openssl

# if build/ cache is stale, reset configure cache
Remove-Item -Recurse -Force build
```

Alternative (manual OpenSSL install):

```powershell
cmake -S . -B build -DOPENSSL_ROOT_DIR="C:/OpenSSL-Win64"
```

If you use a multi-config generator (Visual Studio), remember to build with `--config Release` or `--config Debug`.

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
  --max-candidates 0 \
  --threads 8
```

> Notes:
> - `--target-addresses` is now optional.
> - `--max-candidates 0` means no limit (default behavior).
> - For ETH scanner requests set `ETHERSCAN_API_KEY` in the environment for stable results.
> - You can run only manual balance checks without seed generation by using `--manual-wallets`.

## Manual wallet check mode

Use this mode when you want to verify parser/scanner behavior on known addresses from a text file.

### Input format (`manual_wallets.txt`)

One wallet per line:

```text
btc,1BoatSLRHtKNngkdXEeobR76b53LETtpyT
eth,0xde0B295669a9FD93d5F28D9Ec85E40f4cb697BAe
sol,Vote111111111111111111111111111111111111111
```

Also supported separators: `;` or a single space.
Lines starting with `#` are ignored.

### Run example

```bash
./build/recovery_tool \
  --manual-wallets ./manual_wallets.txt \
  --recovered-wallets ./recovered_wallets.txt \
  --chains "btc,eth,sol"
```

## Быстрый запуск в Windows (.bat)

1. Установите **CMake** и **компилятор C++** (например, Visual Studio Build Tools с MSVC).
2. В корне репозитория запустите `run_project.bat` двойным кликом.
3. При необходимости отредактируйте переменную `TEMPLATE` внутри `run_project.bat` под вашу seed-фразу (через запятую, `*` для неизвестных слов).

Скрипт автоматически:
- настраивает CMake в `build/` (если папки ещё нет),
- собирает проект,
- запускает `build\recovery_tool.exe` с базовыми параметрами.

Для ручной проверки адресов из файла используйте `run_manual_test.bat`.

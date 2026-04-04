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
- `chains::IChainModule` and per-chain module skeletons (`btc`, `eth`, `sol`).
- `engine::Pipeline` and `engine::Matcher` for async candidate processing and address matching.
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
  --target-addresses ./targets.txt \
  --paths-btc "m/84'/0'/0'/0/{i}" \
  --paths-eth "m/44'/60'/0'/0/{i}" \
  --paths-sol "m/44'/501'/{i}'/0'" \
  --scan-limit 20 \
  --max-candidates 100000 \
  --threads 8
```

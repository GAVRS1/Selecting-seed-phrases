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
- `chains::IChainModule` and per-chain modules (`btc`, `eth`, `sol`, `ton`).
- Real seed derivation via BIP-39 PBKDF2-HMAC-SHA512 and chain-specific key trees (BIP-32 for BTC/ETH, SLIP-0010 Ed25519 for SOL).
- Ethereum addresses are output in canonical hex format with `0x` prefix.
- `engine::Pipeline` and `engine::Matcher` for async candidate processing and address matching.
- Per-chain deduplication: once a chain wallet is recovered, that chain is skipped for all next candidates.
- Automatic persistence of generated wallets in `blockchain/address/mnemonic` format.
- Duplicate wallet protection when creating wallet records from seed phrases.
- Manual wallet import mode from a TXT file (`--manual-wallets`) for parser verification.
- Console output without balance values: `wallet || address || seed`.
- Optional persistence of generated wallets to PostgreSQL (`--postgres-conn` + `--postgres-table`).
- `.env` support for PostgreSQL settings (`RECOVERY_POSTGRES_CONN`, `RECOVERY_POSTGRES_TABLE`).
- SQL migrations in `migrations/` with a helper script `scripts/db_migrate.sh`.
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

If `vcpkg` reports `vcpkg.json:1:1: error: Unexpected character; expected value` and shows
something like `on expression: Set-Content -Path .\vcpkg.json ...`, your manifest file was overwritten
with a PowerShell command text. Recreate it as valid JSON:

```powershell
$json = @{
  name = "selecting-seed-phrases"
  "version-string" = "0.1.0"
  dependencies = @("openssl")
} | ConvertTo-Json -Depth 5
[System.IO.File]::WriteAllText(".\vcpkg.json", $json, (New-Object System.Text.UTF8Encoding($false)))
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
  --postgres-conn "postgresql://postgres:postgres@127.0.0.1:5432/recovery" \
  --postgres-table "recovered_wallets" \
  --bip39-passphrase "" \
  --shuffle-words \
  --paths-btc "m/84'/0'/0'/0/{i}" \
  --paths-eth "m/44'/60'/0'/0/{i}" \
  --paths-sol "m/44'/501'/{i}'/0'" \
  --paths-ton "m/44'/607'/0'/{i}'" \
  --scan-limit 20 \
  --max-candidates 0 \
  --threads 8
```

> Notes:
> - `--target-addresses` is now optional.
> - `--max-candidates 0` means no limit (default behavior).
> - `--shuffle-words` randomizes wildcard substitution order to avoid always starting from the same alphabetic prefix.
> - `--shuffle-seed <number>` enables shuffle with a fixed seed for reproducible runs.
> - `--allow-words "abandon,ability,about"` limits wildcard substitutions without editing the original BIP-39 wordlist.
> - You can run only manual wallet imports without seed generation by using `--manual-wallets`.
> - If `--postgres-conn` is set, wallet records are written to PostgreSQL instead of TXT.
> - If `--postgres-conn` is not passed, the app reads `RECOVERY_POSTGRES_CONN` and `RECOVERY_POSTGRES_TABLE` from `--env-file` (default: `.env`) and then from process environment variables.
> - If the wordlist contains fewer than 2048 words, the tool treats it as a narrowed candidate dictionary and disables checksum validation (a warning is printed at startup).

## PostgreSQL: `.env` + migrations

1. Copy `.env.example` to `.env` and edit values:

```bash
cp .env.example .env
```

2. Apply migrations (creates `schema_migrations` and runs all SQL files from `migrations/`):

```bash
./scripts/db_migrate.sh
```

Windows (double-click friendly, with `pause` at the end):

```bat
scripts\db_migrate.bat
```

> Note: `psql` must be installed and available in `PATH` (PostgreSQL client tools).

Optional: custom env file path.

```bash
./scripts/db_migrate.sh .env.prod
```

3. Run the tool. You can either pass PostgreSQL values explicitly or rely on `.env`:

```bash
./build/recovery_tool \
  --env-file .env \
  --template "abandon,ability,*,*,abandon,ability,abandon,ability,abandon,ability,abandon,ability"
```

If you need another table name, set `RECOVERY_POSTGRES_TABLE` in `.env` or use `--postgres-table`.


## Скрипт проверки балансов из PostgreSQL

Добавлен отдельный Python-скрипт: `scripts/check_wallet_balances.py`.

Что делает:

1. Берёт кошельки из таблицы PostgreSQL (`id, blockchain, address, mnemonic`).
2. Проверяет баланс по сети (`btc`, `eth`, `sol`, `ton`).
3. Для каждого успешно проверенного кошелька сначала печатает в консоль строку:
   `blockchain/address/balance/mnemonic`.
4. Если баланс больше нуля — добавляет строку в `recovered_wallets.txt` в формате:
   `blockchain/address/mnemonic`.
5. После успешной проверки удаляет запись из БД **в любом случае** (и при нулевом, и при ненулевом балансе).
6. Если баланс не удалось проверить из-за ошибки API/сети, запись не удаляется (чтобы не потерять данные).

Запуск:

```bash
python3 scripts/check_wallet_balances.py \
  --env-file .env \
  --output recovered_wallets.txt
```

Опции:

- `--postgres-conn` — строка подключения PostgreSQL (иначе берётся `RECOVERY_POSTGRES_CONN` из `.env`/env).
- `--postgres-table` — имя таблицы (по умолчанию `recovered_wallets`).
- `--delay-seconds` — задержка между запросами к API (полезно при rate-limit).
- `--output` — путь к файлу для найденных непустых кошельков.

## Manual wallet check mode

Use this mode when you want to verify parser behavior on known addresses from a text file.

### Input format (`manual_wallets.txt`)

One wallet per line:

```text
btc,1BoatSLRHtKNngkdXEeobR76b53LETtpyT
eth,0xde0B295669a9FD93d5F28D9Ec85E40f4cb697BAe
sol,Vote111111111111111111111111111111111111111
ton,0:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef
```

Also supported separators: `;` or a single space.
Lines starting with `#` are ignored.

### Run example

```bash
./build/recovery_tool \
  --manual-wallets ./manual_wallets.txt \
  --recovered-wallets ./recovered_wallets.txt \
  --chains "btc,eth,sol,ton"
```

## Быстрый запуск в Windows (.bat)

1. Установите **CMake** и **компилятор C++** (например, Visual Studio Build Tools с MSVC).
2. В корне репозитория запустите `run_project.bat` двойным кликом.
3. При необходимости отредактируйте переменные `TEMPLATE` и `MAX_CANDIDATES` внутри `run_project.bat` под вашу seed-фразу и режим перебора (через запятую, `*` для неизвестных слов).
   - По умолчанию `TEMPLATE` = `*,*,*,*,*,*,*,*,*,*,*,*`, то есть все 12 слов берутся из текущего wordlist.
   - Если вы меняете только слова в текстовом файле `data/bip39_english.txt`, оставьте `TEMPLATE` со звёздочками, иначе фиксированные слова в `TEMPLATE` не будут заменяться.
   - `MAX_CANDIDATES=0` — без лимита, поиск идёт пока вы сами не остановите процесс.

Скрипт автоматически:
- настраивает CMake в `build/` (если папки ещё нет),
- собирает проект,
- запускает `build\recovery_tool.exe` для генерации адресов в PostgreSQL (без проверки баланса внутри C++),
- запускает отдельную консоль с `scripts/check_wallet_balances.py` для проверки балансов и очистки обработанных строк.

### Управление количеством и типом консолей через `.env`

В `.env` (или `.env.example` как шаблон) можно включать/выключать сети и задавать количество консолей на сеть:

```dotenv
RECOVERY_ENABLE_BTC=true
RECOVERY_ENABLE_ETH=true
RECOVERY_ENABLE_SOL=false
RECOVERY_ENABLE_TON=false

RECOVERY_CONSOLES_BTC=25
RECOVERY_CONSOLES_ETH=100
RECOVERY_CONSOLES_SOL=0
RECOVERY_CONSOLES_TON=0
```

- Если `RECOVERY_ENABLE_<CHAIN>=false`, эта сеть не запускается.
- Если `RECOVERY_CONSOLES_<CHAIN>=0`, для сети не открывается ни одной консоли.
- Таким образом остаётся **один общий запуск** проекта через `run_project.bat`, а детализация по сетям задаётся в `.env`.

Для ручной проверки адресов из файла используйте `run_manual_test.bat`.

## Подробная установка на Windows (Visual Studio уже установлена)

Ниже — пошаговая инструкция для случая, когда на ПК уже есть Visual Studio, но нет `vcpkg`/OpenSSL.

### 0) Что должно быть установлено

1. **Visual Studio 2022** с рабочей нагрузкой **Desktop development with C++**.
2. **Git for Windows**.
3. **CMake** (если не уверены — поставьте отдельно с https://cmake.org/download/).

Проверка в **PowerShell**:

```powershell
git --version
cmake --version
```

Если команды не находятся — установите соответствующий инструмент и перезапустите терминал.

### 1) Откройте правильный терминал от Visual Studio

Проще всего использовать:

- **x64 Native Tools Command Prompt for VS 2022**  
или
- обычный PowerShell, но запущенный через меню Visual Studio Developer PowerShell.

Это важно, чтобы CMake видел компилятор MSVC.

### 2) Установите vcpkg (один раз)

В PowerShell выполните:

```powershell
git clone https://github.com/microsoft/vcpkg $env:USERPROFILE\vcpkg
& $env:USERPROFILE\vcpkg\bootstrap-vcpkg.bat
setx VCPKG_ROOT "$env:USERPROFILE\vcpkg"
```

После `setx` **закройте терминал и откройте новый**, иначе переменная `VCPKG_ROOT` может не подхватиться.

Проверка:

```powershell
$env:VCPKG_ROOT
Test-Path "$env:VCPKG_ROOT\vcpkg.exe"
```

### 3) Установите OpenSSL через vcpkg (важно: Manifest Mode vs Classic Mode)

Если вы видите ошибку:

`Could not locate a manifest (vcpkg.json) ... This vcpkg distribution does not have a classic mode instance`

это значит, что у вас **manifest-only** сборка vcpkg (часто так в новых установках).

В этом репозитории уже есть `vcpkg.json`.  
Если проект ещё не скачан, сначала выполните шаг 4, затем вернитесь сюда.

Далее выполните:

```powershell
cd <ПАПКА_РЕПОЗИТОРИЯ>
& $env:VCPKG_ROOT\vcpkg install --triplet x64-windows
```

Проверка:

```powershell
& $env:VCPKG_ROOT\vcpkg list | Select-String openssl
```

> Если у вас классическая сборка vcpkg, команда `vcpkg install openssl:x64-windows` тоже может работать.  
> Но для совместимости лучше использовать manifest-подход из примера выше.

### 4) Склонируйте проект (если ещё не скачали)

```powershell
git clone <URL_ВАШЕГО_РЕПОЗИТОРИЯ>
cd <ПАПКА_РЕПОЗИТОРИЯ>
```

### 5) Сконфигурируйте CMake с toolchain vcpkg

```powershell
cmake -S . -B build `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows
```

Если используете генератор Visual Studio (multi-config), это нормально.

### 6) Соберите проект

```powershell
cmake --build build --config Release
```

После успешной сборки exe обычно находится по пути:

- `build\Release\recovery_tool.exe` (для Visual Studio generator), или
- `build\recovery_tool.exe` (для single-config generator).

### 7) Запустите быстрый сценарий (опционально)

Можно использовать готовый bat-скрипт:

```powershell
.\run_project.bat
```

Или запускать exe вручную с параметрами из раздела **Run (example)** выше.

### 8) Частые проблемы и решения

1. **`Could not locate a manifest (vcpkg.json)` / `does not have a classic mode instance`**  
   Вы запускаете команду классического режима в manifest-only vcpkg.  
   Решение:
   - перейдите в папку проекта, где лежит `vcpkg.json`,
   - выполните `vcpkg install --triplet x64-windows`.

2. **`Could NOT find OpenSSL`**  
   Убедитесь, что:
   - зависимости установлены командой `vcpkg install --triplet x64-windows`,
   - вы передали `-DCMAKE_TOOLCHAIN_FILE=...vcpkg.cmake`,
   - используете тот же triplet (`x64-windows`).

3. **CMake не видит компилятор (`cl.exe`)**  
   Откройте именно Developer/Native Tools терминал от Visual Studio.

4. **Старая кеш-конфигурация CMake**  
   Очистите сборку и настройте заново:

   ```powershell
   Remove-Item -Recurse -Force build
   ```

5. **Переменная `VCPKG_ROOT` пустая**  
   После `setx` нужен новый терминал.  
   Временный вариант на текущую сессию:

   ```powershell
   $env:VCPKG_ROOT="$env:USERPROFILE\vcpkg"
   ```

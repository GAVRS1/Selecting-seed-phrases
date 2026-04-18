# 🟠 Bitcoin Checker (DB Version)

Чекер сделан по шаблону других checker-скриптов и работает через **Bitcoin RPC**.

## Возможности

- Проверка BTC баланса по адресам из PostgreSQL.
- Массовая загрузка адресов (`DB_FETCH_LIMIT`) и пакетная проверка (`BATCH_SIZE`).
- Проверка через `scantxoutset` (UTXO RPC) с ретраями и fallback между несколькими RPC URL.
- Режим `all` для запуска через общий сценарий, как в других чекерах.
- Экспорт в CSV + запись активных кошельков в `result/recovered_wallets.txt`.
- Удаление успешно проверенных записей из БД пачкой.

## Установка

```bash
cd scripts/bitcoin-checker
npm install
```

## Unified root .env

```env
POSTGRES_CONN=postgresql://user:password@127.0.0.1:5432/recovery
RECOVERY_BTC_POSTGRES_SOURCE_TABLE=recovered_wallets_btc
DB_FETCH_LIMIT=500
DELETE_PROCESSED_WALLETS=true

BATCH_SIZE=100
RETRY_ATTEMPTS=3
RETRY_DELAY=1000
ENABLE_LOGGING=true
ENABLE_PROGRESS_BAR=true

# Можно один URL:
BTC_RPC_URL=http://user:password@127.0.0.1:8332

# Или несколько URL через запятую/; / перенос строки:
BTC_RPC_URLS=http://user:pass@127.0.0.1:8332,http://user:pass@10.0.0.2:8332
```

## Запуск

```bash
npm start           # режим bitcoin
npm run bitcoin     # режим bitcoin
npm run all         # multi-checker режим
```

## Выходные файлы

- `result/bitcoin.csv`
- `result/bitcoin_all_networks_balances.csv` (в режиме `all`)
- `result/recovered_wallets.txt`
- `result/bitcoin_checker_errors.log`

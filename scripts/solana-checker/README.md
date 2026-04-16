# 🟣 Solana Checker (DB Version)

Чекер сделан по шаблону `evm-checker`, но работает **только с Solana**.

## Возможности

- Проверка SOL и SPL токенов.
- Batch-режим (проверка адресов пачками).
- Multi-checker режим (`all`) для параллельной проверки всех доступных конфигов (сейчас это Solana).
- Загрузка адресов из PostgreSQL и удаление обработанных записей.
- Экспорт в CSV + запись активных кошельков в `recovered_wallets.txt`.

## Установка

```bash
cd scripts/solana-checker
npm install
```

## .env

```env
POSTGRES_CONN=postgresql://user:password@127.0.0.1:5432/recovery
POSTGRES_SOURCE_TABLE=recovered_wallets_sol
DB_FETCH_LIMIT=500
DELETE_PROCESSED_WALLETS=true

BATCH_SIZE=200
RETRY_ATTEMPTS=3
RETRY_DELAY=1000
ENABLE_LOGGING=true
ENABLE_PROGRESS_BAR=true

# Необязательно: кастомный RPC
SOLANA_RPC_URL=https://api.mainnet-beta.solana.com

# Необязательно: дополнительные SPL mint через запятую
SOLANA_TOKEN_MINTS=So11111111111111111111111111111111111111112
```

## Запуск

```bash
npm start            # режим solana
npm run solana       # режим solana
npm run all          # multi-checker режим
```

## Выходные файлы

- `results/solana.csv`
- `results/all_networks_balances.csv` (в режиме `all`)
- `recovered_wallets.txt`

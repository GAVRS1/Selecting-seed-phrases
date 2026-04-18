# 🟣 Solana Checker (DB Version)

Чекер сделан по шаблону `evm-checker`, но работает **только с Solana**.

## Возможности

- Проверка SOL и SPL токенов.
- Batch-режим (проверка адресов пачками).
- Multi-checker режим (`all`) для параллельной проверки всех доступных конфигов (сейчас это Solana).
- Загрузка адресов из PostgreSQL и удаление обработанных записей.
- Экспорт в CSV + запись активных кошельков в `result/recovered_wallets.txt`.

## Установка

```bash
cd scripts/solana-checker
npm install
```

## Unified root .env

```env
POSTGRES_CONN=postgresql://user:password@127.0.0.1:5432/recovery
RECOVERY_SOL_POSTGRES_SOURCE_TABLE=recovered_wallets_sol
DB_FETCH_LIMIT=500
DELETE_PROCESSED_WALLETS=true

BATCH_SIZE=200
RETRY_ATTEMPTS=3
RETRY_DELAY=1000
ENABLE_LOGGING=true
ENABLE_PROGRESS_BAR=true
```

## Запуск

```bash
npm start            # режим solana
npm run solana       # режим solana
npm run all          # multi-checker режим
```

## Выходные файлы

- `result/solana.csv`
- `result/solana_all_networks_balances.csv` (в режиме `all`)
- `result/recovered_wallets.txt`


Дополнительная конфигурация сетей/токенов хранится в `config/checkers/checkers.config.json`.
Данные для прокси и других входов храните в `data/`.

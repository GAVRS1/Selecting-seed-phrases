# 🐆 EVM Checker (DB Version)

[🇬🇧 English](#english-version) | [🇷🇺 Русский](#русская-версия)

---

<a name="english-version"></a>
# 🇬🇧 English Version

## 📦 Installation

```bash
npm install
npm install pg
```

---

## ⚙️ Configuration

### 🔑 .env Setup (REQUIRED)

Create `.env` file:

```env
POSTGRES_CONN=postgresql://user:password@127.0.0.1:5432/recovery
POSTGRES_SOURCE_TABLE=recovered_wallets_evm
DB_FETCH_LIMIT=500
DELETE_PROCESSED_WALLETS=true

BATCH_SIZE=200
RETRY_ATTEMPTS=3
RETRY_DELAY=1000
ENABLE_LOGGING=true
ENABLE_PROGRESS_BAR=true
```

---

## 🗄 Database Structure

Expected table format:

```sql
id          BIGINT
address     TEXT
blockchain  TEXT
mnemonic    TEXT
created_at  TIMESTAMP
```

👉 Only `id` and `address` are required.

---

## 🚀 How It Works

1. Fetch wallets from DB (LIMIT N)
2. Check balances across networks
3. Save results to CSV
4. Delete processed wallets from DB (batch)

✔ Zero balance → deleted  
✔ Non-zero → saved + deleted  
❌ Errors → NOT deleted  

---

## ▶️ Usage

### 🔹 Interactive mode
```bash
npm start
```

---

### 🔹 Run specific network
```bash
npm start ethereum
npm start bnb
npm start polygon
```

---

### 🔹 Run ALL networks
```bash
npm start -- all
```

---

## 📊 Results

Saved to:
```
/results/
```

Single network:
```
ethereum.csv
```

All networks:
```
all_networks_balances.csv
```

---

## ⚡ Features

- ⚡ Multicall batching
- 🌐 27 EVM networks
- 🧠 Retry system (exponential backoff)
- 📦 Batch DB processing
- 🗑 Auto-delete processed wallets
- 🚫 No API limits (RPC based)

---

## 🔥 Important

- Uses RPC, not public APIs
- Supports ERC20 tokens
- Designed for large datasets

---

---

<a name="русская-версия"></a>
# 🇷🇺 Русская версия

## 📦 Установка

```bash
npm install
npm install pg
```

---

## ⚙️ Настройки

### 🔑 .env (ОБЯЗАТЕЛЬНО)

Создай `.env`:

```env
POSTGRES_CONN=postgresql://user:password@127.0.0.1:5432/recovery
POSTGRES_SOURCE_TABLE=recovered_wallets_evm
DB_FETCH_LIMIT=500
DELETE_PROCESSED_WALLETS=true

BATCH_SIZE=200
RETRY_ATTEMPTS=3
RETRY_DELAY=1000
ENABLE_LOGGING=true
ENABLE_PROGRESS_BAR=true
```

---

## 🗄 Структура базы данных

```sql
id          BIGINT
address     TEXT
blockchain  TEXT
mnemonic    TEXT
created_at  TIMESTAMP
```

👉 Для работы нужны только:
```
id + address
```

---

## 🚀 Как работает

1. Берёт N кошельков из БД
2. Проверяет балансы
3. Сохраняет результат
4. Удаляет проверенные записи

✔ 0 баланс → удаляется  
✔ есть баланс → сохраняется + удаляется  
❌ ошибка → НЕ удаляется  

---

## ▶️ Запуск

### 🔹 Интерактивный режим
```bash
npm start
```

---

### 🔹 Одна сеть
```bash
npm start ethereum
npm start bnb
npm start polygon
```

---

### 🔹 ВСЕ сети
```bash
npm start -- all
```

---

## 📊 Результаты

```
/results/
```

---

## ⚡ Возможности

- ⚡ Multicall (очень быстро)
- 🌐 27 сетей
- 🧠 ретраи с backoff
- 📦 работа пачками из БД
- 🗑 авто-удаление проверенных
- 🚫 нет лимитов API

---

## 🔥 Важно

- Использует RPC
- Поддержка ERC20 токенов
- Подходит для больших объёмов
- Работает как мультичекер

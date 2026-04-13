CREATE TABLE IF NOT EXISTS seed_phrases_btc (
    id BIGSERIAL PRIMARY KEY,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    mnemonic TEXT NOT NULL UNIQUE
);

CREATE TABLE IF NOT EXISTS seed_phrases_evm (
    id BIGSERIAL PRIMARY KEY,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    mnemonic TEXT NOT NULL UNIQUE
);

CREATE TABLE IF NOT EXISTS seed_phrases_sol (
    id BIGSERIAL PRIMARY KEY,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    mnemonic TEXT NOT NULL UNIQUE
);

CREATE TABLE IF NOT EXISTS recovered_wallets_btc (
    id BIGSERIAL PRIMARY KEY,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    blockchain TEXT NOT NULL,
    address TEXT NOT NULL,
    mnemonic TEXT NOT NULL,
    UNIQUE (blockchain, address, mnemonic)
);

CREATE TABLE IF NOT EXISTS recovered_wallets_evm (
    id BIGSERIAL PRIMARY KEY,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    blockchain TEXT NOT NULL,
    address TEXT NOT NULL,
    mnemonic TEXT NOT NULL,
    UNIQUE (blockchain, address, mnemonic)
);

CREATE TABLE IF NOT EXISTS recovered_wallets_sol (
    id BIGSERIAL PRIMARY KEY,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    blockchain TEXT NOT NULL,
    address TEXT NOT NULL,
    mnemonic TEXT NOT NULL,
    UNIQUE (blockchain, address, mnemonic)
);

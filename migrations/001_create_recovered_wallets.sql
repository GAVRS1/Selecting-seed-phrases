CREATE TABLE IF NOT EXISTS recovered_wallets (
    id BIGSERIAL PRIMARY KEY,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    blockchain TEXT NOT NULL,
    address TEXT NOT NULL,
    mnemonic TEXT NOT NULL,
    UNIQUE (blockchain, address, mnemonic)
);

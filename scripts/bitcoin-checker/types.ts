export enum Network {
  BITCOIN = 'bitcoin',
}

export interface WalletData {
  id?: number;
  address: string;
  mnemonic?: string;
}

export interface WalletBalance {
  wallet: WalletData;
  balances: Map<string, string>;
  errors?: Map<string, Error>;
}

export interface CheckerOptions {
  batchSize?: number;
  retryAttempts?: number;
  retryDelay?: number;
  showProgress?: boolean;
  logErrors?: boolean;
}

export interface CheckerConfig {
  network: Network;
  wallets: WalletData[];
  options?: CheckerOptions;
}

export interface NetworkConfig {
  name: string;
  nativeCurrency: string;
  rpcUrls: string[];
}

export interface AppConfig {
  postgresConnectionString?: string;
  postgresSourceTable: string;
  dbFetchLimit: number;
  deleteProcessedWallets: boolean;
  defaultBatchSize: number;
  defaultRetryAttempts: number;
  defaultRetryDelay: number;
  enableLogging: boolean;
  enableProgressBar: boolean;
}

export interface CheckerStats {
  totalWallets: number;
  successfulChecks: number;
  failedChecks: number;
  duration: number;
}

export interface AllNetworksCheckResult {
  network: Network;
  results: WalletBalance[];
  tokenHeaders: string[];
}

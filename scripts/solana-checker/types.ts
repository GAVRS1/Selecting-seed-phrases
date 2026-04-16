export enum Network {
  SOLANA = 'solana',
}

export type Config = {
  [key in Network]: {
    RPC_URL: string;
    COLUMNS: string[];
    TOKENS: Record<string, string>;
    NAME: string;
    NATIVE_CURRENCY: string;
  };
};

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
  tokens: string[];
  options?: CheckerOptions;
}

export interface CheckerStats {
  totalWallets: number;
  totalTokens: number;
  successfulChecks: number;
  failedChecks: number;
  duration: number;
}

export interface AllNetworksCheckResult {
  network: Network;
  results: WalletBalance[];
  tokenHeaders: string[];
}

export interface ResultExporter {
  exportSingleNetwork(data: WalletBalance[], config: CheckerConfig, tokenHeaders: string[]): Promise<void>;
  exportAllNetworks(allNetworksResults: AllNetworksCheckResult[], filename?: string): Promise<void>;
}

export interface NetworkConfig {
  name: string;
  nativeCurrency: string;
  rpcUrl: string;
  tokens: string[];
}

export interface AppConfig {
  postgresConnectionString?: string;
  postgresSourceTable: string;
  dbFetchLimit: number;
  deleteProcessedWallets: boolean;
  defaultNetwork: Network;
  defaultBatchSize: number;
  defaultRetryAttempts: number;
  defaultRetryDelay: number;
  enableLogging: boolean;
  enableProgressBar: boolean;
  customRpcUrl?: string;
}

import { AppConfig, Network, NetworkConfig } from '../types';
import { CONFIG } from '../config';
import fs from 'fs';
import path from 'path';
import SharedPaths from '../../checker-shared/paths.ts';


interface SolanaConfigFile {
  solana?: {
    rpcUrls?: string[];
    tokenMints?: string[];
  };
}

export class ConfigManager {
  private config: AppConfig = {
    postgresConnectionString: undefined,
    postgresSourceTable: 'recovered_wallets_sol',
    dbFetchLimit: 500,
    deleteProcessedWallets: true,
    defaultNetwork: Network.SOLANA,
    defaultBatchSize: 200,
    defaultRetryAttempts: 3,
    defaultRetryDelay: 1000,
    enableLogging: true,
    enableProgressBar: true,
    customRpcUrl: undefined,
  };

  private networkConfig: NetworkConfig;

  constructor() {
    this.loadEnv();

    const dynamicTokens = this.parseExtraTokens();
    const mergedTokens = [...CONFIG[Network.SOLANA].COLUMNS, ...dynamicTokens];

    this.networkConfig = {
      name: CONFIG[Network.SOLANA].NAME,
      nativeCurrency: CONFIG[Network.SOLANA].NATIVE_CURRENCY,
      rpcUrl: this.config.customRpcUrl || CONFIG[Network.SOLANA].RPC_URL,
      tokens: Array.from(new Set(mergedTokens)),
    };
  }

  private loadEnv(): void {
    const env = SharedPaths.loadEnvFile(SharedPaths.ROOT_ENV_FILE);
    const get = (...keys: string[]): string | undefined => {
      for (const key of keys) {
        if (env[key]) return env[key];
        if (process.env[key]) return process.env[key];
      }
      return undefined;
    };

    this.config.postgresConnectionString = get('POSTGRES_CONN', 'RECOVERY_POSTGRES_CONN') || this.config.postgresConnectionString;
    this.config.postgresSourceTable = get('POSTGRES_SOURCE_TABLE', 'RECOVERY_SOL_POSTGRES_SOURCE_TABLE') || this.config.postgresSourceTable;
    this.config.dbFetchLimit = Number(get('DB_FETCH_LIMIT', 'RECOVERY_DB_FETCH_LIMIT') || this.config.dbFetchLimit);
    this.config.deleteProcessedWallets = (get('DELETE_PROCESSED_WALLETS', 'RECOVERY_DELETE_PROCESSED_WALLETS') || `${this.config.deleteProcessedWallets}`).toLowerCase() === 'true';
    this.config.defaultBatchSize = Number(get('BATCH_SIZE', 'RECOVERY_BATCH_SIZE') || this.config.defaultBatchSize);
    this.config.defaultRetryAttempts = Number(get('RETRY_ATTEMPTS', 'RECOVERY_RETRY_ATTEMPTS') || this.config.defaultRetryAttempts);
    this.config.defaultRetryDelay = Number(get('RETRY_DELAY', 'RECOVERY_RETRY_DELAY') || this.config.defaultRetryDelay);
    this.config.enableLogging = (get('ENABLE_LOGGING', 'RECOVERY_ENABLE_LOGGING') || `${this.config.enableLogging}`).toLowerCase() === 'true';
    this.config.enableProgressBar = (get('ENABLE_PROGRESS_BAR', 'RECOVERY_ENABLE_PROGRESS_BAR') || `${this.config.enableProgressBar}`).toLowerCase() === 'true';
    const rootConfig = this.loadRootCheckerConfig();
    this.config.customRpcUrl = get('SOLANA_RPC_URL', 'RECOVERY_SOLANA_RPC_URL') || rootConfig.solana?.rpcUrls?.[0] || this.config.customRpcUrl;
  }

  private loadRootCheckerConfig(): SolanaConfigFile {
    const configPath = path.join(SharedPaths.PROJECT_ROOT, 'config', 'checkers', 'checkers.config.json');
    if (!fs.existsSync(configPath)) {
      return {};
    }

    try {
      return JSON.parse(fs.readFileSync(configPath, 'utf-8')) as SolanaConfigFile;
    } catch {
      return {};
    }
  }

  private parseExtraTokens(): string[] {
    const env = SharedPaths.loadEnvFile(SharedPaths.ROOT_ENV_FILE);
    const rootConfig = this.loadRootCheckerConfig();
    const raw = env.SOLANA_TOKEN_MINTS || env.RECOVERY_SOLANA_TOKEN_MINTS || process.env.SOLANA_TOKEN_MINTS || process.env.RECOVERY_SOLANA_TOKEN_MINTS || '';

    const envTokens = raw
      .split(',')
      .map((value) => value.trim())
      .filter(Boolean);

    return [...(rootConfig.solana?.tokenMints || []), ...envTokens];
  }

  getAppConfig(): AppConfig {
    return { ...this.config };
  }

  getNetworkConfig(_network: Network): NetworkConfig {
    return this.networkConfig;
  }
}

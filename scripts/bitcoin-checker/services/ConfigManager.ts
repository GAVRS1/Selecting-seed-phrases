import fs from 'fs';
import path from 'path';
import SharedPaths from '../../checker-shared/paths.ts';
import { AppConfig, Network, NetworkConfig } from '../types';
import { CONFIG } from '../config';

interface CheckerConfigFile {
  bitcoin?: {
    rpcUrls?: string[];
  };
}

export class ConfigManager {
  private config: AppConfig = {
    postgresConnectionString: undefined,
    postgresSourceTable: 'recovered_wallets_btc',
    dbFetchLimit: 500,
    deleteProcessedWallets: true,
    defaultBatchSize: 100,
    defaultRetryAttempts: 3,
    defaultRetryDelay: 1000,
    enableLogging: true,
    enableProgressBar: true,
  };

  private networkConfig: NetworkConfig;

  constructor() {
    this.loadEnv();
    this.networkConfig = {
      name: CONFIG.bitcoin.NAME,
      nativeCurrency: CONFIG.bitcoin.NATIVE_CURRENCY,
      rpcUrls: this.resolveRpcUrls(),
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
    this.config.postgresSourceTable = get('POSTGRES_SOURCE_TABLE', 'RECOVERY_BTC_POSTGRES_SOURCE_TABLE') || this.config.postgresSourceTable;
    this.config.dbFetchLimit = Number(get('DB_FETCH_LIMIT', 'RECOVERY_DB_FETCH_LIMIT') || this.config.dbFetchLimit);
    this.config.deleteProcessedWallets = (get('DELETE_PROCESSED_WALLETS', 'RECOVERY_DELETE_PROCESSED_WALLETS') || `${this.config.deleteProcessedWallets}`).toLowerCase() === 'true';
    this.config.defaultBatchSize = Number(get('BATCH_SIZE', 'RECOVERY_BATCH_SIZE') || this.config.defaultBatchSize);
    this.config.defaultRetryAttempts = Number(get('RETRY_ATTEMPTS', 'RECOVERY_RETRY_ATTEMPTS') || this.config.defaultRetryAttempts);
    this.config.defaultRetryDelay = Number(get('RETRY_DELAY', 'RECOVERY_RETRY_DELAY') || this.config.defaultRetryDelay);
    this.config.enableLogging = (get('ENABLE_LOGGING', 'RECOVERY_ENABLE_LOGGING') || `${this.config.enableLogging}`).toLowerCase() === 'true';
    this.config.enableProgressBar = (get('ENABLE_PROGRESS_BAR', 'RECOVERY_ENABLE_PROGRESS_BAR') || `${this.config.enableProgressBar}`).toLowerCase() === 'true';
  }

  private loadRootCheckerConfig(): CheckerConfigFile {
    const configPath = path.join(SharedPaths.PROJECT_ROOT, 'config', 'checkers', 'checkers.config.json');
    if (!fs.existsSync(configPath)) return {};

    try {
      return JSON.parse(fs.readFileSync(configPath, 'utf-8')) as CheckerConfigFile;
    } catch {
      return {};
    }
  }

  private resolveRpcUrls(): string[] {
    const env = SharedPaths.loadEnvFile(SharedPaths.ROOT_ENV_FILE);
    const configFile = this.loadRootCheckerConfig();

    const envRaw = env.BTC_RPC_URLS
      || env.RECOVERY_BTC_RPC_URLS
      || env.BTC_RPC_URL
      || env.RECOVERY_BTC_RPC_URL
      || process.env.BTC_RPC_URLS
      || process.env.RECOVERY_BTC_RPC_URLS
      || process.env.BTC_RPC_URL
      || process.env.RECOVERY_BTC_RPC_URL
      || '';

    const envRpcUrls = envRaw
      .split(/[\n,;]/)
      .map((value) => value.trim())
      .filter(Boolean);

    const merged = [...envRpcUrls, ...(configFile.bitcoin?.rpcUrls || []), ...CONFIG.bitcoin.RPC_URLS];
    return Array.from(new Set(merged));
  }

  getAppConfig(): AppConfig {
    return { ...this.config };
  }

  getNetworkConfig(_network: Network): NetworkConfig {
    return this.networkConfig;
  }
}

import fs from 'fs';
import path from 'path';
import { AppConfig, Network, NetworkConfig } from '../types';
import { CONFIG } from '../config';

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
    const envPath = path.resolve(process.cwd(), '.env');
    let env: Record<string, string> = {};

    if (fs.existsSync(envPath)) {
      env = Object.fromEntries(
        fs
          .readFileSync(envPath, 'utf-8')
          .split('\n')
          .map((line) => line.trim())
          .filter((line) => line && !line.startsWith('#') && line.includes('='))
          .map((line) => {
            const [key, ...rest] = line.split('=');
            return [key.trim(), rest.join('=').replace(/^['\"]|['\"]$/g, '').trim()];
          })
      );
    }

    const get = (key: string): string | undefined => env[key] || process.env[key];

    this.config.postgresConnectionString = get('POSTGRES_CONN') || this.config.postgresConnectionString;
    this.config.postgresSourceTable = get('POSTGRES_SOURCE_TABLE') || this.config.postgresSourceTable;
    this.config.dbFetchLimit = Number(get('DB_FETCH_LIMIT') || this.config.dbFetchLimit);
    this.config.deleteProcessedWallets = (get('DELETE_PROCESSED_WALLETS') || `${this.config.deleteProcessedWallets}`).toLowerCase() === 'true';
    this.config.defaultBatchSize = Number(get('BATCH_SIZE') || this.config.defaultBatchSize);
    this.config.defaultRetryAttempts = Number(get('RETRY_ATTEMPTS') || this.config.defaultRetryAttempts);
    this.config.defaultRetryDelay = Number(get('RETRY_DELAY') || this.config.defaultRetryDelay);
    this.config.enableLogging = (get('ENABLE_LOGGING') || `${this.config.enableLogging}`).toLowerCase() === 'true';
    this.config.enableProgressBar = (get('ENABLE_PROGRESS_BAR') || `${this.config.enableProgressBar}`).toLowerCase() === 'true';
    this.config.customRpcUrl = get('SOLANA_RPC_URL') || this.config.customRpcUrl;
  }

  private parseExtraTokens(): string[] {
    const raw = process.env.SOLANA_TOKEN_MINTS || '';
    return raw
      .split(',')
      .map((value) => value.trim())
      .filter(Boolean);
  }

  getAppConfig(): AppConfig {
    return { ...this.config };
  }

  getNetworkConfig(_network: Network): NetworkConfig {
    return this.networkConfig;
  }
}

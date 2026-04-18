import { Network } from '../types';
import { CONFIG } from '../config';
import fs from 'fs';
import path from 'path';
import SharedPaths from '../../checker-shared/paths.ts';

export interface AppConfig {
  defaultNetwork?: Network;
  defaultBatchSize: number;
  defaultRetryAttempts: number;
  defaultRetryDelay: number;
  customRpcUrls: Partial<Record<Network, string>>;
  enableLogging: boolean;
  enableProgressBar: boolean;
  postgresConnectionString?: string;
  postgresSourceTable: string;
  dbFetchLimit: number;
  deleteProcessedWallets: boolean;
}

export interface NetworkConfig {
  name: string;
  nativeCurrency: string;
  rpcUrl: string;
  multicallContract?: string;
  tokens: string[];
}



interface EvmConfigFile {
  evm?: {
    networkOverrides?: Record<
      string,
      {
        rpcUrls?: string[];
        multicallContract?: string;
        tokens?: Record<string, string>;
      }
    >;
  };
}

export class ConfigManager {
  private config: AppConfig;
  private networkConfigs: Map<Network, NetworkConfig>;

  constructor() {
    this.config = this.getDefaultConfig();
    this.networkConfigs = new Map();
    this.loadEnvironmentVariables();
    this.initializeNetworkConfigs();
  }

  private getDefaultConfig(): AppConfig {
    return {
      postgresConnectionString: process.env.POSTGRES_CONN || process.env.RECOVERY_POSTGRES_CONN,
      postgresSourceTable: 'recovered_wallets_evm',
      dbFetchLimit: 500,
      deleteProcessedWallets: true,
      defaultBatchSize: 200,
      defaultRetryAttempts: 3,
      defaultRetryDelay: 1000,
      customRpcUrls: {},
      enableLogging: true,
      enableProgressBar: true,
    };
  }

  private loadEnvironmentVariables(): void {
    const envVars = SharedPaths.loadEnvFile(SharedPaths.ROOT_ENV_FILE);
    this.applyEnvironmentVariables(envVars);
  }

  private pick(envVars: Record<string, string>, ...keys: string[]): string | undefined {
    for (const key of keys) {
      if (envVars[key]) return envVars[key];
      if (process.env[key]) return process.env[key];
    }
    return undefined;
  }

  private applyEnvironmentVariables(envVars: Record<string, string>): void {
    this.config.postgresConnectionString =
      this.pick(envVars, 'POSTGRES_CONN', 'RECOVERY_POSTGRES_CONN') || this.config.postgresConnectionString;

    this.config.postgresSourceTable =
      this.pick(envVars, 'POSTGRES_SOURCE_TABLE', 'RECOVERY_EVM_POSTGRES_SOURCE_TABLE') || this.config.postgresSourceTable;

    this.config.dbFetchLimit = Number(this.pick(envVars, 'DB_FETCH_LIMIT', 'RECOVERY_DB_FETCH_LIMIT') || this.config.dbFetchLimit);

    this.config.deleteProcessedWallets =
      (this.pick(envVars, 'DELETE_PROCESSED_WALLETS', 'RECOVERY_DELETE_PROCESSED_WALLETS') || `${this.config.deleteProcessedWallets}`).toLowerCase() === 'true';

    const defaultNetwork = this.pick(envVars, 'DEFAULT_NETWORK');
    if (defaultNetwork) {
      this.config.defaultNetwork = defaultNetwork as Network;
    }

    this.config.defaultBatchSize = Number(this.pick(envVars, 'BATCH_SIZE', 'RECOVERY_BATCH_SIZE') || this.config.defaultBatchSize);
    this.config.defaultRetryAttempts = Number(this.pick(envVars, 'RETRY_ATTEMPTS', 'RECOVERY_RETRY_ATTEMPTS') || this.config.defaultRetryAttempts);
    this.config.defaultRetryDelay = Number(this.pick(envVars, 'RETRY_DELAY', 'RECOVERY_RETRY_DELAY') || this.config.defaultRetryDelay);

    this.config.enableLogging = (this.pick(envVars, 'ENABLE_LOGGING', 'RECOVERY_ENABLE_LOGGING') || `${this.config.enableLogging}`).toLowerCase() === 'true';
    this.config.enableProgressBar = (this.pick(envVars, 'ENABLE_PROGRESS_BAR', 'RECOVERY_ENABLE_PROGRESS_BAR') || `${this.config.enableProgressBar}`).toLowerCase() === 'true';

    Object.keys(envVars).forEach((key) => {
      if (key.startsWith('RPC_URL_') || key.startsWith('RECOVERY_EVM_RPC_URL_')) {
        const networkName = key
          .replace('RPC_URL_', '')
          .replace('RECOVERY_EVM_RPC_URL_', '')
          .toLowerCase()
          .replace(/_/g, '-');
        const network = Object.values(Network).find((n) => n === networkName);
        if (network) {
          this.config.customRpcUrls[network] = envVars[key];
        }
      }
    });
  }

  private loadRootCheckerConfig(): EvmConfigFile {
    const configPath = path.join(SharedPaths.PROJECT_ROOT, 'config', 'checkers', 'checkers.config.json');
    if (!fs.existsSync(configPath)) {
      return {};
    }

    try {
      return JSON.parse(fs.readFileSync(configPath, 'utf-8')) as EvmConfigFile;
    } catch {
      return {};
    }
  }

  private initializeNetworkConfigs(): void {
    const rootConfig = this.loadRootCheckerConfig();

    Object.entries(CONFIG).forEach(([networkKey, networkData]) => {
      const network = networkKey as Network;
      const override = rootConfig.evm?.networkOverrides?.[network];
      const mergedTokens = override?.tokens ? { ...networkData.TOKENS, ...override.tokens } : networkData.TOKENS;
      const tokenColumns = ['native', ...Object.values(mergedTokens)];

      this.networkConfigs.set(network, {
        name: networkData.NAME || network,
        nativeCurrency: networkData.NATIVE_CURRENCY || 'ETH',
        rpcUrl: this.config.customRpcUrls[network] || override?.rpcUrls?.[0] || networkData.RPC_URL,
        multicallContract: override?.multicallContract || networkData.MULTICALL3_CONTRACT,
        tokens: tokenColumns,
      });
    });
  }

  getAppConfig(): AppConfig {
    return { ...this.config };
  }

  getNetworkConfig(network: Network): NetworkConfig | undefined {
    return this.networkConfigs.get(network);
  }

  getAllNetworkConfigs(): Map<Network, NetworkConfig> {
    return new Map(this.networkConfigs);
  }
}

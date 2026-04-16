import { Network } from '../types';
import { CONFIG } from '../config';
import fs from 'fs';
import path from 'path';

export interface AppConfig {
  // Основные настройки
  defaultNetwork?: Network;
  defaultBatchSize: number;
  defaultRetryAttempts: number;
  defaultRetryDelay: number;
  
  // Файловые пути
  walletsFile: string;
  resultsDir: string;
  
  // RPC настройки
  customRpcUrls: Partial<Record<Network, string>>;
  
  // Логирование
  enableLogging: boolean;
  
  // UI настройки
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

export class ConfigManager {
  private config: AppConfig;
  private networkConfigs: Map<Network, NetworkConfig>;
  private envLoaded: boolean = false;

  constructor() {
    this.config = this.getDefaultConfig();
    this.networkConfigs = new Map();
    this.loadEnvironmentVariables();
    this.initializeNetworkConfigs();
  }

  // Получить конфигурацию по умолчанию
  private getDefaultConfig(): AppConfig {
    return {

      postgresConnectionString: process.env.POSTGRES_CONN,
      postgresSourceTable: 'recovered_wallets_evm',
      dbFetchLimit: 500,
      deleteProcessedWallets: true,

      defaultBatchSize: 200,
      defaultRetryAttempts: 3,
      defaultRetryDelay: 1000,
      
      walletsFile: 'wallets.txt',
      resultsDir: 'results',
      
      customRpcUrls: {},
      
      enableLogging: true,
      
      enableProgressBar: true
    };
  }

  // Загрузка переменных окружения
  private loadEnvironmentVariables(): void {
    try {
      const envPath = path.join(process.cwd(), '.env');
      if (fs.existsSync(envPath)) {
        const envContent = fs.readFileSync(envPath, 'utf-8');
        const envVars = this.parseEnvFile(envContent);
        this.applyEnvironmentVariables(envVars);
        this.envLoaded = true;
      }
    } catch (error) {
      console.warn('⚠️  Не удалось загрузить .env файл:', error);
    }
  }

  // Парсинг .env файла
  private parseEnvFile(content: string): Record<string, string> {
    const envVars: Record<string, string> = {};
    
    content.split('\n').forEach(line => {
      line = line.trim();
      if (line && !line.startsWith('#')) {
        const [key, ...valueParts] = line.split('=');
        if (key && valueParts.length > 0) {
          const value = valueParts.join('=').trim();
          envVars[key.trim()] = value.replace(/^["']|["']$/g, ''); // Убираем кавычки
        }
      }
    });
    
    return envVars;
  }

  // Применение переменных окружения
private applyEnvironmentVariables(envVars: Record<string, string>): void {
  if (envVars.POSTGRES_CONN || process.env.POSTGRES_CONN) {
    this.config.postgresConnectionString = envVars.POSTGRES_CONN || process.env.POSTGRES_CONN;
  }

  if (envVars.POSTGRES_SOURCE_TABLE || process.env.POSTGRES_SOURCE_TABLE) {
    this.config.postgresSourceTable =
      envVars.POSTGRES_SOURCE_TABLE || process.env.POSTGRES_SOURCE_TABLE || this.config.postgresSourceTable;
  }

  if (envVars.DB_FETCH_LIMIT || process.env.DB_FETCH_LIMIT) {
    this.config.dbFetchLimit =
      parseInt(envVars.DB_FETCH_LIMIT || process.env.DB_FETCH_LIMIT || '') || this.config.dbFetchLimit;
  }

  if (envVars.DELETE_PROCESSED_WALLETS || process.env.DELETE_PROCESSED_WALLETS) {
    this.config.deleteProcessedWallets =
      (envVars.DELETE_PROCESSED_WALLETS || process.env.DELETE_PROCESSED_WALLETS || '').toLowerCase() === 'true';
  }

  if (envVars.DEFAULT_NETWORK) {
    this.config.defaultNetwork = envVars.DEFAULT_NETWORK as Network;
  }
  if (envVars.BATCH_SIZE) {
    this.config.defaultBatchSize = parseInt(envVars.BATCH_SIZE) || this.config.defaultBatchSize;
  }
  if (envVars.RETRY_ATTEMPTS) {
    this.config.defaultRetryAttempts = parseInt(envVars.RETRY_ATTEMPTS) || this.config.defaultRetryAttempts;
  }
  if (envVars.RETRY_DELAY) {
    this.config.defaultRetryDelay = parseInt(envVars.RETRY_DELAY) || this.config.defaultRetryDelay;
  }

  if (envVars.WALLETS_FILE) {
    this.config.walletsFile = envVars.WALLETS_FILE;
  }
  if (envVars.RESULTS_DIR) {
    this.config.resultsDir = envVars.RESULTS_DIR;
  }

  if (envVars.ENABLE_LOGGING) {
    this.config.enableLogging = envVars.ENABLE_LOGGING.toLowerCase() === 'true';
  }

  if (envVars.ENABLE_PROGRESS_BAR) {
    this.config.enableProgressBar = envVars.ENABLE_PROGRESS_BAR.toLowerCase() === 'true';
  }

  Object.keys(envVars).forEach(key => {
    if (key.startsWith('RPC_URL_')) {
      const networkName = key.replace('RPC_URL_', '').toLowerCase().replace(/_/g, '-');
      const network = Object.values(Network).find(n => n === networkName);
      if (network) {
        this.config.customRpcUrls[network] = envVars[key];
      }
    }
  });
}
  // Инициализация конфигураций сетей
  private initializeNetworkConfigs(): void {
    Object.entries(CONFIG).forEach(([networkKey, networkData]) => {
      const network = networkKey as Network;
      
      const networkConfig: NetworkConfig = {
        name: this.getNetworkName(network),
        nativeCurrency: this.getNativeCurrency(network),
        rpcUrl: this.config.customRpcUrls[network] || networkData.RPC_URL,
        multicallContract: networkData.MULTICALL3_CONTRACT,
        tokens: networkData.COLUMNS
      };
      
      this.networkConfigs.set(network, networkConfig);
    });
  }

  // Получить название сети
  private getNetworkName(network: Network): string {
    return CONFIG[network]?.NAME || network;
  }

  // Получить нативную валюту
  private getNativeCurrency(network: Network): string {
    return CONFIG[network]?.NATIVE_CURRENCY || "ETH";
  }

  // Публичные методы для получения конфигурации
  getAppConfig(): AppConfig {
    return { ...this.config };
  }

  getNetworkConfig(network: Network): NetworkConfig | undefined {
    return this.networkConfigs.get(network);
  }

  getAllNetworkConfigs(): Map<Network, NetworkConfig> {
    return new Map(this.networkConfigs);
  }

  // Валидация конфигурации
  validateConfig(): { isValid: boolean; errors: string[] } {
    const errors: string[] = [];

    // Проверка основных настроек
    if (this.config.defaultBatchSize <= 0) {
      errors.push('defaultBatchSize должен быть больше 0');
    }
    if (this.config.defaultRetryAttempts < 0) {
      errors.push('defaultRetryAttempts не может быть отрицательным');
    }
    if (this.config.defaultRetryDelay < 0) {
      errors.push('defaultRetryDelay не может быть отрицательным');
    }

    // Проверка файловых путей
    if (!this.config.walletsFile) {
      errors.push('walletsFile не может быть пустым');
    }
    if (!this.config.resultsDir) {
      errors.push('resultsDir не может быть пустым');
    }

    // Проверка сетевых конфигураций
    this.networkConfigs.forEach((config, network) => {
      if (!config.rpcUrl) {
        errors.push(`RPC URL для сети ${network} не задан`);
      }
      if (!config.name) {
        errors.push(`Название для сети ${network} не задано`);
      }
      if (!config.nativeCurrency) {
        errors.push(`Нативная валюта для сети ${network} не задана`);
      }
    });

    return {
      isValid: errors.length === 0,
      errors
    };
  }

  // Получить информацию о загруженной конфигурации
  getConfigInfo(): {
    envLoaded: boolean;
    networksCount: number;
    customRpcCount: number;
    validationResult: { isValid: boolean; errors: string[] };
  } {
    return {
      envLoaded: this.envLoaded,
      networksCount: this.networkConfigs.size,
      customRpcCount: Object.keys(this.config.customRpcUrls).length,
      validationResult: this.validateConfig()
    };
  }
} 
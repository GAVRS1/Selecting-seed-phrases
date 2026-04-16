import fs from 'fs';
import path from 'path';
import { Connection, PublicKey } from '@solana/web3.js';
import { CheckerConfig, CheckerStats, WalletBalance } from '../types';
import { SplTokenBalanceProvider, SolNativeBalanceProvider } from './BalanceService';
import { UIService } from './UIService';

export class WalletChecker {
  private static readonly RPC_MAX_BATCH_SIZE = 100;
  private readonly stats: CheckerStats;
  private readonly startTime: number;
  private readonly ui = new UIService();
  private tokenHeaders: string[] = [];

  constructor(private readonly config: CheckerConfig, private readonly rpcUrl: string) {
    this.stats = {
      totalWallets: config.wallets.length,
      totalTokens: config.tokens.length,
      successfulChecks: 0,
      failedChecks: 0,
      duration: 0,
    };
    this.startTime = Date.now();
  }

  getTokenHeaders(): string[] {
    return this.tokenHeaders;
  }

  getStats(): CheckerStats {
    return this.stats;
  }

  async check(): Promise<WalletBalance[]> {
    const connection = new Connection(this.rpcUrl, 'confirmed');
    const addresses = this.config.wallets.map((wallet) => wallet.address);

    const providers = this.config.tokens.map((token) => {
      if (token === 'native') {
        return { token, header: 'SOL', provider: new SolNativeBalanceProvider(connection) };
      }
      return { token, header: `SPL:${token.slice(0, 4)}...${token.slice(-4)}`, provider: new SplTokenBalanceProvider(connection, token) };
    });

    this.tokenHeaders = providers.map((provider) => provider.header);

    if (this.config.options?.showProgress) {
      this.ui.startProgress(`Проверка ${addresses.length} адресов и ${providers.length} активов`);
    }

    let completed = 0;
    const total = providers.length * addresses.length;

    const tokenResults = await Promise.all(
      providers.map(async ({ token, provider, header }) => {
        try {
          const balances = await this.getBatchedBalances(provider, addresses);
          this.stats.successfulChecks += addresses.length;
          completed += addresses.length;
          this.ui.updateProgress({ current: completed, total, currentItem: header });
          return { token, balances, error: undefined };
        } catch (error) {
          this.stats.failedChecks += addresses.length;
          completed += addresses.length;
          this.ui.updateProgress({ current: completed, total, currentItem: header });
          this.logError(`Ошибка ${token}: ${error}`);
          return { token, balances: new Map(addresses.map((addr) => [addr, '0'])), error: error as Error };
        }
      })
    );

    const results: WalletBalance[] = this.config.wallets.map((wallet) => ({
      wallet,
      balances: new Map<string, string>(),
      errors: new Map<string, Error>(),
    }));

    tokenResults.forEach(({ token, balances, error }) => {
      addresses.forEach((address, idx) => {
        results[idx].balances.set(token, balances.get(address) || '0');
        if (error) {
          results[idx].errors?.set(token, error);
        }
      });
    });

    this.stats.duration = Date.now() - this.startTime;
    if (this.config.options?.showProgress) {
      this.ui.succeedProgress(`Готово за ${(this.stats.duration / 1000).toFixed(2)}s`);
    }

    return results;
  }

  private async getBatchedBalances(
    provider: SolNativeBalanceProvider | SplTokenBalanceProvider,
    addresses: string[]
  ): Promise<Map<string, string>> {
    const configuredBatchSize = this.config.options?.batchSize || WalletChecker.RPC_MAX_BATCH_SIZE;
    const batchSize = Math.min(configuredBatchSize, WalletChecker.RPC_MAX_BATCH_SIZE);
    const batches: string[][] = [];

    for (let i = 0; i < addresses.length; i += batchSize) {
      batches.push(addresses.slice(i, i + batchSize));
    }

    const chunks = await Promise.all(batches.map((batch) => this.withRetry(() => this.safeCheck(provider, batch))));
    const merged = new Map<string, string>();
    chunks.forEach((chunk) => chunk.forEach((value, key) => merged.set(key, value)));
    return merged;
  }

  private async safeCheck(
    provider: SolNativeBalanceProvider | SplTokenBalanceProvider,
    addresses: string[]
  ): Promise<Map<string, string>> {
    const valid = addresses.filter((address) => {
      try {
        new PublicKey(address);
        return true;
      } catch {
        return false;
      }
    });

    const balances = await provider.getBatchBalances(valid);
    addresses.forEach((address) => {
      if (!balances.has(address)) {
        balances.set(address, '0');
      }
    });
    return balances;
  }

  private async withRetry<T>(fn: () => Promise<T>): Promise<T> {
    const attempts = this.config.options?.retryAttempts || 3;
    const delay = this.config.options?.retryDelay || 1000;

    let lastError: unknown;
    for (let attempt = 1; attempt <= attempts; attempt++) {
      try {
        return await fn();
      } catch (error) {
        lastError = error;
        if (attempt < attempts) {
          await new Promise((resolve) => setTimeout(resolve, delay * attempt));
        }
      }
    }

    throw lastError;
  }

  private logError(message: string): void {
    if (!this.config.options?.logErrors) return;

    fs.mkdirSync('results', { recursive: true });
    const logPath = path.join('results', 'error_log.txt');
    fs.appendFileSync(logPath, `[${new Date().toISOString()}] ${message}\n`, 'utf-8');
  }
}

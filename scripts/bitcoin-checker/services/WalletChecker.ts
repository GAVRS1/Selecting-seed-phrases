import { CheckerConfig, CheckerStats, WalletBalance } from '../types';
import { UIService } from './UIService';

interface JsonRpcSuccess<T> {
  result: T;
  error?: undefined;
}

interface JsonRpcError {
  code: number;
  message: string;
}

interface JsonRpcFailure {
  result?: undefined;
  error: JsonRpcError;
}

type JsonRpcResponse<T> = JsonRpcSuccess<T> | JsonRpcFailure;

interface ScanUtxo {
  desc?: string;
  amount?: number;
}

interface ScanTxOutSetResult {
  success: boolean;
  unspents?: ScanUtxo[];
}

export class WalletChecker {
  private readonly ui = new UIService();
  private readonly stats: CheckerStats;
  private readonly startTime: number;

  constructor(private readonly config: CheckerConfig, private readonly rpcUrls: string[]) {
    this.stats = {
      totalWallets: config.wallets.length,
      successfulChecks: 0,
      failedChecks: 0,
      duration: 0,
    };
    this.startTime = Date.now();
  }

  async check(): Promise<WalletBalance[]> {
    const addresses = this.config.wallets.map((wallet) => wallet.address);
    const batchSize = Math.max(1, this.config.options?.batchSize || 100);
    const totalBatches = Math.ceil(addresses.length / batchSize);

    const balancesByAddress = new Map<string, string>(addresses.map((address) => [address, '0']));
    const errorsByAddress = new Map<string, Error>();

    if (this.config.options?.showProgress) {
      this.ui.startProgress(`BTC RPC проверка ${addresses.length} адресов`);
    }

    let doneBatches = 0;
    for (let i = 0; i < addresses.length; i += batchSize) {
      const batch = addresses.slice(i, i + batchSize);
      try {
        const batchBalances = await this.withRetry(() => this.scanBatch(batch));
        for (const address of batch) {
          balancesByAddress.set(address, batchBalances.get(address) || '0');
        }
        this.stats.successfulChecks += batch.length;
      } catch (error) {
        const normalized = error instanceof Error ? error : new Error(String(error));
        for (const address of batch) {
          errorsByAddress.set(address, normalized);
          balancesByAddress.set(address, '0');
        }
        this.stats.failedChecks += batch.length;
      }

      doneBatches += 1;
      this.ui.updateProgress({ current: doneBatches, total: totalBatches, currentItem: 'BTC' });
    }

    this.stats.duration = Date.now() - this.startTime;
    if (this.config.options?.showProgress) {
      this.ui.succeedProgress(`Готово за ${(this.stats.duration / 1000).toFixed(2)}s`);
    }

    return this.config.wallets.map((wallet) => {
      const err = errorsByAddress.get(wallet.address);
      return {
        wallet,
        balances: new Map([['native', balancesByAddress.get(wallet.address) || '0']]),
        errors: err ? new Map([['native', err]]) : new Map(),
      };
    });
  }

  private async scanBatch(addresses: string[]): Promise<Map<string, string>> {
    const descriptors = addresses.map((address) => `addr(${address})`);
    const response = await this.callRpc<ScanTxOutSetResult>('scantxoutset', ['start', descriptors]);

    if (!response.success) {
      throw new Error('scantxoutset вернул success=false');
    }

    const balances = new Map<string, number>(addresses.map((address) => [address, 0]));

    for (const utxo of response.unspents || []) {
      const address = this.extractAddressFromDescriptor(utxo.desc || '');
      if (!address) continue;
      if (!balances.has(address)) continue;

      const current = balances.get(address) || 0;
      balances.set(address, current + Number(utxo.amount || 0));
    }

    return new Map(Array.from(balances.entries()).map(([address, amount]) => [address, amount.toString()]));
  }

  private extractAddressFromDescriptor(desc: string): string | null {
    const match = desc.match(/addr\(([^)]+)\)/);
    return match?.[1] || null;
  }

  private async callRpc<T>(method: string, params: unknown[]): Promise<T> {
    if (!this.rpcUrls.length) {
      throw new Error('Не задан ни один BTC RPC URL');
    }

    let lastError: Error | null = null;

    for (let i = 0; i < this.rpcUrls.length; i++) {
      const rpcUrl = this.rpcUrls[i];
      try {
        const endpoint = new URL(rpcUrl);
        const authHeader = endpoint.username || endpoint.password
          ? `Basic ${Buffer.from(`${decodeURIComponent(endpoint.username)}:${decodeURIComponent(endpoint.password)}`).toString('base64')}`
          : '';

        endpoint.username = '';
        endpoint.password = '';

        const body = {
          jsonrpc: '1.0',
          id: `${Date.now()}-${Math.random()}`,
          method,
          params,
        };

        const res = await fetch(endpoint.toString(), {
          method: 'POST',
          headers: {
            'Content-Type': 'application/json',
            ...(authHeader ? { Authorization: authHeader } : {}),
          },
          body: JSON.stringify(body),
        });

        if (!res.ok) {
          throw new Error(`HTTP ${res.status}`);
        }

        const json = (await res.json()) as JsonRpcResponse<T>;
        if ('error' in json && json.error) {
          throw new Error(`RPC ${json.error.code}: ${json.error.message}`);
        }

        return (json as JsonRpcSuccess<T>).result;
      } catch (error) {
        lastError = error instanceof Error ? error : new Error(String(error));
      }
    }

    throw lastError || new Error('BTC RPC error');
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
}

import { ConfigManager } from './services/ConfigManager';
import { CsvExporter } from './services/CsvExporter';
import { UIService } from './services/UIService';
import { WalletChecker } from './services/WalletChecker';
import { AllNetworksChecker } from './services/AllNetworksChecker';
import { DatabaseWalletRepository } from './services/DatabaseWalletRepository';
import { AllNetworksCheckResult, Network, WalletBalance, WalletData } from './types';
import { CONFIG } from './config';
import fs from 'fs';
import path from 'path';

const RECOVERED_OUTPUT_PATH = path.resolve(process.cwd(), 'recovered_wallets.txt');

function isPositiveBalance(value: string | undefined): boolean {
  if (!value) {
    return false;
  }
  const parsed = Number(value);
  return Number.isFinite(parsed) && parsed > 0;
}

function formatEthBalance(value: string | undefined): string {
  if (!value || !Number.isFinite(Number(value))) {
    return 'ETH:0';
  }

  return `ETH:${value}`;
}

function appendRecoveredWallets(rows: string[]): void {
  if (rows.length === 0) {
    return;
  }
  fs.appendFileSync(RECOVERED_OUTPUT_PATH, `${rows.join('\n')}\n`, 'utf-8');
}

function buildRecoveredLine(wallet: WalletData, displayBalance: string): string {
  return `eth/${wallet.address}/${wallet.mnemonic || 'test'}/${displayBalance}`;
}

function collectRecoveredFromSingleNetwork(results: WalletBalance[]): string[] {
  return results
    .filter((result) => isPositiveBalance(result.balances.get('native')))
    .map((result) => buildRecoveredLine(result.wallet, formatEthBalance(result.balances.get('native'))));
}

function collectRecoveredFromAllNetworks(allResults: AllNetworksCheckResult[], wallets: WalletData[]): string[] {
  const nativeBalanceByAddress = new Map<string, string>();
  for (const networkResult of allResults) {
    for (const result of networkResult.results) {
      if (!isPositiveBalance(result.balances.get('native'))) {
        continue;
      }
      const address = result.wallet.address.toLowerCase();
      if (!nativeBalanceByAddress.has(address)) {
        nativeBalanceByAddress.set(address, result.balances.get('native') || '0');
      }
    }
  }

  return wallets
    .filter((wallet) => nativeBalanceByAddress.has(wallet.address.toLowerCase()))
    .map((wallet) =>
      buildRecoveredLine(wallet, formatEthBalance(nativeBalanceByAddress.get(wallet.address.toLowerCase())))
    );
}

function hasWalletErrors(result: WalletBalance): boolean {
  return Boolean(result.errors && result.errors.size > 0);
}

function getDeletableIdsSingleNetwork(wallets: WalletData[], results: WalletBalance[]): number[] {
  const successAddresses = new Set(
    results
      .filter((result) => !hasWalletErrors(result))
      .map((result) => result.wallet.address.toLowerCase())
  );

  return wallets
    .filter((wallet) => successAddresses.has(wallet.address.toLowerCase()))
    .map((wallet) => wallet.id)
    .filter((id): id is number => typeof id === 'number');
}

function getDeletableIdsAllNetworks(wallets: WalletData[], allResults: AllNetworksCheckResult[]): number[] {
  const expectedNetworks = Object.values(Network).filter((network) => Boolean(CONFIG[network])).length;
  const hasMissingNetworkResults = allResults.length < expectedNetworks;

  return wallets
    .filter((wallet) => {
      const address = wallet.address.toLowerCase();
      const walletResults = allResults
        .map((networkResult) => networkResult.results.find((result) => result.wallet.address.toLowerCase() === address))
        .filter((result): result is WalletBalance => Boolean(result));

      if (hasMissingNetworkResults || walletResults.length !== expectedNetworks) {
        return false;
      }

      return walletResults.every((result) => !hasWalletErrors(result));
    })
    .map((wallet) => wallet.id)
    .filter((id): id is number => typeof id === 'number');
}

async function main(): Promise<void> {
  const configManager = new ConfigManager();
  const appConfig = configManager.getAppConfig();
  const ui = new UIService();
  const exporter = new CsvExporter();

  if (!appConfig.postgresConnectionString) {
    throw new Error('POSTGRES_CONN не задан');
  }

  const repo = new DatabaseWalletRepository({
    connectionString: appConfig.postgresConnectionString,
    sourceTable: appConfig.postgresSourceTable,
    fetchLimit: appConfig.dbFetchLimit,
  });

  try {
    ui.showHeader();

    const wallets = await repo.fetchBatch();
    if (wallets.length === 0) {
      ui.showWarning('В таблице нет кошельков для проверки.');
      return;
    }

    ui.showInfo(`Загружено из БД: ${wallets.length} кошельков`);

    const cliNetwork = process.argv[2] as Network | 'all' | undefined;

    let checkedWalletIds: number[] = [];

    if (cliNetwork && cliNetwork !== 'all') {
      const networkConfig = configManager.getNetworkConfig(cliNetwork);
      if (!networkConfig) {
        throw new Error(`Неизвестная сеть: ${cliNetwork}`);
      }

      const checker = new WalletChecker({
        network: cliNetwork,
        wallets,
        tokens: networkConfig.tokens,
        options: {
          showProgress: appConfig.enableProgressBar,
          logErrors: appConfig.enableLogging,
          batchSize: appConfig.defaultBatchSize,
          retryAttempts: appConfig.defaultRetryAttempts,
          retryDelay: appConfig.defaultRetryDelay,
        },
      });

      const results = await checker.check();
      await exporter.exportSingleNetwork(results, {
        network: cliNetwork,
        wallets,
        tokens: networkConfig.tokens,
        options: checker.getStats() as any,
      } as any, checker.getTokenHeaders());

      const recoveredRows = collectRecoveredFromSingleNetwork(results);
      appendRecoveredWallets(recoveredRows);
      if (recoveredRows.length > 0) {
        ui.showSuccess(`Добавлено в recovered_wallets.txt: ${recoveredRows.length} кошельков.`);
      } else {
        ui.showWarning('Кошельки с положительным ETH балансом не найдены.');
      }

      checkedWalletIds = getDeletableIdsSingleNetwork(wallets, results);
    } else {
      const allChecker = new AllNetworksChecker(wallets, configManager, ui);
      const allResults = await allChecker.checkAllNetworks();
      await exporter.exportAllNetworks(allResults);
      const recoveredRows = collectRecoveredFromAllNetworks(allResults, wallets);
      appendRecoveredWallets(recoveredRows);
      if (recoveredRows.length > 0) {
        ui.showSuccess(`Добавлено в recovered_wallets.txt: ${recoveredRows.length} кошельков.`);
      } else {
        ui.showWarning('Кошельки с положительным ETH балансом не найдены.');
      }

      checkedWalletIds = getDeletableIdsAllNetworks(wallets, allResults);
    }

    if (appConfig.deleteProcessedWallets && checkedWalletIds.length > 0) {
      const deleted = await repo.deleteBatch(checkedWalletIds);
      ui.showSuccess(`Удалено из БД: ${deleted} кошельков одной пачкой.`);
    } else {
      ui.showWarning('Удаление отключено, нет id для удаления или кошельки содержат ошибки проверки.');
    }
  } finally {
    await repo.close();
  }
}

main().catch((error) => {
  console.error('Критическая ошибка:', error);
  process.exit(1);
});

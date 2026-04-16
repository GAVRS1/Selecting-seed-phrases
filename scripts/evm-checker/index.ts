import { ConfigManager } from './services/ConfigManager';
import { CsvExporter } from './services/CsvExporter';
import { UIService } from './services/UIService';
import { WalletChecker } from './services/WalletChecker';
import { AllNetworksChecker } from './services/AllNetworksChecker';
import { DatabaseWalletRepository } from './services/DatabaseWalletRepository';
import { AllNetworksCheckResult, Network, WalletBalance, WalletData } from './types';
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

      checkedWalletIds = wallets
        .filter((wallet) => results.some((r) => r.wallet.address.toLowerCase() === wallet.address.toLowerCase()))
        .map((wallet) => wallet.id)
        .filter((id): id is number => typeof id === 'number');
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

      checkedWalletIds = wallets
        .map((wallet) => wallet.id)
        .filter((id): id is number => typeof id === 'number');
    }

    if (appConfig.deleteProcessedWallets && checkedWalletIds.length > 0) {
      const deleted = await repo.deleteBatch(checkedWalletIds);
      ui.showSuccess(`Удалено из БД: ${deleted} кошельков одной пачкой.`);
    } else {
      ui.showWarning('Удаление отключено или нет id для удаления.');
    }
  } finally {
    await repo.close();
  }
}

main().catch((error) => {
  console.error('Критическая ошибка:', error);
  process.exit(1);
});

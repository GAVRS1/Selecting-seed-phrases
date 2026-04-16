import fs from 'fs';
import path from 'path';
import { ConfigManager } from './services/ConfigManager';
import { CsvExporter } from './services/CsvExporter';
import { DatabaseWalletRepository } from './services/DatabaseWalletRepository';
import { UIService } from './services/UIService';
import { WalletChecker } from './services/WalletChecker';
import { AllNetworksChecker } from './services/AllNetworksChecker';
import { Network } from './types';

const RECOVERED_OUTPUT_PATH = path.resolve(process.cwd(), 'recovered_wallets.txt');

function isPositive(value: string | undefined): boolean {
  return Number(value || '0') > 0;
}

function appendRecovered(rows: string[]): void {
  if (!rows.length) return;
  fs.appendFileSync(RECOVERED_OUTPUT_PATH, `${rows.join('\n')}\n`, 'utf-8');
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

    const mode = (process.argv[2] || 'solana').toLowerCase();
    const checkedIds = wallets.map((wallet) => wallet.id).filter((id): id is number => typeof id === 'number');

    if (mode === 'all') {
      const allChecker = new AllNetworksChecker(wallets, configManager, ui);
      const allResults = await allChecker.checkAllNetworks();
      await exporter.exportAllNetworks(allResults);

      const recovered = allResults[0]?.results
        .filter((row) => isPositive(row.balances.get('native')))
        .map((row) => `sol/${row.wallet.address}/${row.wallet.mnemonic || 'test'}/SOL:${row.balances.get('native') || '0'}`) || [];

      appendRecovered(recovered);
      ui.showSuccess(`Добавлено в recovered_wallets.txt: ${recovered.length} кошельков.`);
    } else {
      const network = Network.SOLANA;
      const networkConfig = configManager.getNetworkConfig(network);

      const checker = new WalletChecker(
        {
          network,
          wallets,
          tokens: networkConfig.tokens,
          options: {
            showProgress: appConfig.enableProgressBar,
            logErrors: appConfig.enableLogging,
            batchSize: appConfig.defaultBatchSize,
            retryAttempts: appConfig.defaultRetryAttempts,
            retryDelay: appConfig.defaultRetryDelay,
          },
        },
        networkConfig.rpcUrl
      );

      const results = await checker.check();
      await exporter.exportSingleNetwork(results, { network, wallets, tokens: networkConfig.tokens, options: {} }, checker.getTokenHeaders());

      const recovered = results
        .filter((row) => isPositive(row.balances.get('native')))
        .map((row) => `sol/${row.wallet.address}/${row.wallet.mnemonic || 'test'}/SOL:${row.balances.get('native') || '0'}`);

      appendRecovered(recovered);
      ui.showSuccess(`Добавлено в recovered_wallets.txt: ${recovered.length} кошельков.`);
    }

    if (appConfig.deleteProcessedWallets && checkedIds.length) {
      const deleted = await repo.deleteBatch(checkedIds);
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

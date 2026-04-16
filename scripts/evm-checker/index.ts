import { ConfigManager } from './services/ConfigManager';
import { CsvExporter } from './services/CsvExporter';
import { UIService } from './services/UIService';
import { WalletChecker } from './services/WalletChecker';
import { AllNetworksChecker } from './services/AllNetworksChecker';
import { DatabaseWalletRepository } from './services/DatabaseWalletRepository';
import { Network } from './types';

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

      checkedWalletIds = wallets
        .filter((wallet) => results.some((r) => r.wallet.address.toLowerCase() === wallet.address.toLowerCase()))
        .map((wallet) => wallet.id)
        .filter((id): id is number => typeof id === 'number');
    } else {
      const allChecker = new AllNetworksChecker(wallets, configManager, ui);
      const allResults = await allChecker.checkAllNetworks();
      await exporter.exportAllNetworks(allResults);

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

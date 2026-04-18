import fs from 'fs';
import path from 'path';
import SharedPaths from '../checker-shared/paths.ts';
import { AllNetworksChecker } from './services/AllNetworksChecker';
import { ConfigManager } from './services/ConfigManager';
import { CsvExporter } from './services/CsvExporter';
import { DatabaseWalletRepository } from './services/DatabaseWalletRepository';
import { UIService } from './services/UIService';
import { Network, WalletBalance } from './types';
import { WalletChecker } from './services/WalletChecker';

const RECOVERED_OUTPUT_PATH = path.join(SharedPaths.ROOT_RESULT_DIR, 'recovered_wallets.txt');
const ERRORS_OUTPUT_PATH = path.join(SharedPaths.ROOT_RESULT_DIR, 'bitcoin_checker_errors.log');

function isPositive(value: string | undefined): boolean {
  return Number(value || '0') > 0;
}

function appendLines(filePath: string, rows: string[]): void {
  if (!rows.length) return;
  fs.appendFileSync(filePath, `${rows.join('\n')}\n`, 'utf-8');
}

function appendErrors(source: string, lines: string[]): void {
  if (!lines.length) return;
  const now = new Date().toISOString();
  appendLines(
    ERRORS_OUTPUT_PATH,
    lines.map((line) => `[${now}] [${source}] ${line}`)
  );
}

function collectErrors(rows: WalletBalance[], network: string): string[] {
  const errors: string[] = [];
  for (const row of rows) {
    if (!row.errors || row.errors.size === 0) continue;
    for (const [token, error] of row.errors.entries()) {
      errors.push(`${network};${row.wallet.address};${token};${error.message}`);
    }
  }
  return errors;
}

function collectSuccessfulIds(rows: WalletBalance[]): number[] {
  return Array.from(new Set(rows
    .filter((row) => (row.errors?.size || 0) === 0)
    .map((row) => row.wallet.id)
    .filter((id): id is number => typeof id === 'number')));
}

async function main(): Promise<void> {
  SharedPaths.ensureRootDirectories();

  const configManager = new ConfigManager();
  const appConfig = configManager.getAppConfig();
  const ui = new UIService();
  const exporter = new CsvExporter();

  if (!appConfig.postgresConnectionString) {
    throw new Error('POSTGRES_CONN/RECOVERY_POSTGRES_CONN не задан');
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

    const mode = (process.argv[2] || 'bitcoin').toLowerCase();
    let checkedIds: number[] = [];

    if (mode === 'all') {
      const allChecker = new AllNetworksChecker(wallets, configManager, ui);
      const allResults = await allChecker.checkAllNetworks();
      await exporter.exportAllNetworks(allResults);

      for (const result of allResults) {
        appendErrors('all', collectErrors(result.results, result.network));
      }

      const recovered = allResults[0]?.results
        .filter((row) => isPositive(row.balances.get('native')))
        .map((row) => `btc/${row.wallet.address}/${row.wallet.mnemonic || 'test'}/BTC:${row.balances.get('native') || '0'}`) || [];

      appendLines(RECOVERED_OUTPUT_PATH, recovered);
      ui.showSuccess(`Добавлено в result/recovered_wallets.txt: ${recovered.length} кошельков.`);
      checkedIds = collectSuccessfulIds(allResults.flatMap((result) => result.results));
    } else {
      const network = Network.BITCOIN;
      const networkConfig = configManager.getNetworkConfig(network);
      const checker = new WalletChecker(
        {
          network,
          wallets,
          options: {
            showProgress: appConfig.enableProgressBar,
            logErrors: appConfig.enableLogging,
            batchSize: appConfig.defaultBatchSize,
            retryAttempts: appConfig.defaultRetryAttempts,
            retryDelay: appConfig.defaultRetryDelay,
          },
        },
        networkConfig.rpcUrls
      );

      const results = await checker.check();
      await exporter.exportSingleNetwork(results);
      appendErrors('single', collectErrors(results, network));

      const recovered = results
        .filter((row) => isPositive(row.balances.get('native')))
        .map((row) => `btc/${row.wallet.address}/${row.wallet.mnemonic || 'test'}/BTC:${row.balances.get('native') || '0'}`);

      appendLines(RECOVERED_OUTPUT_PATH, recovered);
      ui.showSuccess(`Добавлено в result/recovered_wallets.txt: ${recovered.length} кошельков.`);
      checkedIds = collectSuccessfulIds(results);
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
  SharedPaths.ensureRootDirectories();
  appendErrors('fatal', [error instanceof Error ? error.message : String(error)]);
  console.error('Критическая ошибка:', error);
  process.exit(1);
});

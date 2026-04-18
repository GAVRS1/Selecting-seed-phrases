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
import SharedPaths from '../checker-shared/paths.ts';

const RECOVERED_OUTPUT_PATH = path.join(SharedPaths.ROOT_RESULT_DIR, 'recovered_wallets.txt');
const ERRORS_OUTPUT_PATH = path.join(SharedPaths.ROOT_RESULT_DIR, 'evm_checker_errors.log');

function isPositiveBalance(value: string | undefined): boolean {
  if (!value) return false;
  const parsed = Number(value);
  return Number.isFinite(parsed) && parsed > 0;
}

function formatEthBalance(value: string | undefined): string {
  if (!value || !Number.isFinite(Number(value))) return 'ETH:0';
  return `ETH:${value}`;
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
      if (!isPositiveBalance(result.balances.get('native'))) continue;
      const address = result.wallet.address.toLowerCase();
      if (!nativeBalanceByAddress.has(address)) {
        nativeBalanceByAddress.set(address, result.balances.get('native') || '0');
      }
    }
  }

  return wallets
    .filter((wallet) => nativeBalanceByAddress.has(wallet.address.toLowerCase()))
    .map((wallet) => buildRecoveredLine(wallet, formatEthBalance(nativeBalanceByAddress.get(wallet.address.toLowerCase()))));
}

function hasWalletErrors(result: WalletBalance): boolean {
  return Boolean(result.errors && result.errors.size > 0);
}

function collectErrors(results: WalletBalance[], network: string): string[] {
  const rows: string[] = [];
  for (const result of results) {
    if (!result.errors || result.errors.size === 0) continue;
    for (const [token, error] of result.errors.entries()) {
      rows.push(`${network};${result.wallet.address};${token};${error.message}`);
    }
  }
  return rows;
}

function getDeletableIdsSingleNetwork(wallets: WalletData[], results: WalletBalance[]): number[] {
  const successAddresses = new Set(results.filter((result) => !hasWalletErrors(result)).map((result) => result.wallet.address.toLowerCase()));

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
    const cliNetwork = process.argv[2] as Network | 'all' | undefined;
    let lastProcessedId: number | undefined;
    let processedWalletsTotal = 0;
    let batchNumber = 0;
    const allSingleNetworkResults: WalletBalance[] = [];
    const allWalletsForSingleNetwork: WalletData[] = [];
    let singleNetworkTokenHeaders: string[] = [];
    const allNetworksResultsByNetwork = new Map<Network, AllNetworksCheckResult>();
    const idsToDelete = new Set<number>();

    while (true) {
      const wallets = await repo.fetchBatch(lastProcessedId);
      if (wallets.length === 0) {
        break;
      }

      batchNumber += 1;
      processedWalletsTotal += wallets.length;
      lastProcessedId = wallets[wallets.length - 1]?.id;
      ui.showInfo(`Пачка #${batchNumber}: загружено ${wallets.length} кошельков (всего обработано: ${processedWalletsTotal}).`);

      if (cliNetwork && cliNetwork !== 'all') {
        const networkConfig = configManager.getNetworkConfig(cliNetwork);
        if (!networkConfig) {
          throw new Error(`Неизвестная сеть: ${cliNetwork}`);
        }

        const checker = new WalletChecker({
          network: cliNetwork,
          wallets,
          tokens: networkConfig.tokens,
          rpcUrl: networkConfig.rpcUrl,
          multicallContract: networkConfig.multicallContract,
          nativeCurrency: networkConfig.nativeCurrency,
          options: {
            showProgress: appConfig.enableProgressBar,
            logErrors: appConfig.enableLogging,
            batchSize: appConfig.defaultBatchSize,
            retryAttempts: appConfig.defaultRetryAttempts,
            retryDelay: appConfig.defaultRetryDelay,
          },
        });

        const results = await checker.check();
        if (singleNetworkTokenHeaders.length === 0) {
          singleNetworkTokenHeaders = checker.getTokenHeaders();
        }
        appendErrors('single', collectErrors(results, cliNetwork));

        const recoveredRows = collectRecoveredFromSingleNetwork(results);
        appendLines(RECOVERED_OUTPUT_PATH, recoveredRows);
        ui.showSuccess(`Пачка #${batchNumber}: добавлено в result/recovered_wallets.txt ${recoveredRows.length} кошельков.`);

        getDeletableIdsSingleNetwork(wallets, results).forEach((id) => idsToDelete.add(id));
        allSingleNetworkResults.push(...results);
        allWalletsForSingleNetwork.push(...wallets);
      } else {
        const allChecker = new AllNetworksChecker(wallets, configManager, ui);
        const allResults = await allChecker.checkAllNetworks();

        for (const networkResult of allResults) {
          appendErrors('all', collectErrors(networkResult.results, networkResult.network));
          const existing = allNetworksResultsByNetwork.get(networkResult.network);
          if (existing) {
            existing.results.push(...networkResult.results);
          } else {
            allNetworksResultsByNetwork.set(networkResult.network, {
              network: networkResult.network,
              tokenHeaders: networkResult.tokenHeaders,
              results: [...networkResult.results],
            });
          }
        }

        const recoveredRows = collectRecoveredFromAllNetworks(allResults, wallets);
        appendLines(RECOVERED_OUTPUT_PATH, recoveredRows);
        ui.showSuccess(`Пачка #${batchNumber}: добавлено в result/recovered_wallets.txt ${recoveredRows.length} кошельков.`);

        getDeletableIdsAllNetworks(wallets, allResults).forEach((id) => idsToDelete.add(id));
      }
    }

    if (processedWalletsTotal === 0) {
      ui.showWarning('В таблице нет кошельков для проверки.');
      return;
    }

    if (cliNetwork && cliNetwork !== 'all') {
      const networkConfig = configManager.getNetworkConfig(cliNetwork);
      if (!networkConfig) {
        throw new Error(`Неизвестная сеть: ${cliNetwork}`);
      }
      await exporter.exportSingleNetwork(allSingleNetworkResults, {
        network: cliNetwork,
        wallets: allWalletsForSingleNetwork,
        tokens: networkConfig.tokens,
        options: {},
      } as any, singleNetworkTokenHeaders);
    } else {
      await exporter.exportAllNetworks(Array.from(allNetworksResultsByNetwork.values()), 'all_networks_balances.csv');
    }

    if (appConfig.deleteProcessedWallets && idsToDelete.size > 0) {
      const deleted = await repo.deleteBatch(Array.from(idsToDelete));
      ui.showSuccess(`Удалено из БД: ${deleted} кошельков.`);
    } else {
      ui.showWarning('Удаление включено, но из БД ничего не удалено.');
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

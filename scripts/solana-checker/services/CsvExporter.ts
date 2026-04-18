import fs from 'fs';
import path from 'path';
import { AllNetworksCheckResult, CheckerConfig, ResultExporter, WalletBalance } from '../types';
import SharedPaths from '../../checker-shared/paths.ts';

export class CsvExporter implements ResultExporter {
  private readonly DELIMITER = ';';
  private readonly RESULTS_FOLDER = SharedPaths.ROOT_RESULT_DIR;

  async exportSingleNetwork(data: WalletBalance[], config: CheckerConfig, tokenHeaders: string[]): Promise<void> {
    SharedPaths.ensureRootDirectories();
    const fullPath = path.join(this.RESULTS_FOLDER, `${config.network}.csv`);
    const rows: string[] = [];

    rows.push(['Address', ...tokenHeaders].join(this.DELIMITER));

    for (const row of data) {
      const balances = config.tokens.map((token) => row.balances.get(token) || '0');
      rows.push([row.wallet.address, ...balances].join(this.DELIMITER));
    }

    fs.writeFileSync(fullPath, `${rows.join('\n')}\n`, 'utf-8');
  }

  async exportAllNetworks(allNetworksResults: AllNetworksCheckResult[], filename = 'all_networks_balances.csv'): Promise<void> {
    const solana = allNetworksResults[0];
    if (!solana) {
      return;
    }

    SharedPaths.ensureRootDirectories();
    const fullPath = path.join(this.RESULTS_FOLDER, filename);

    const tokenKeys = Array.from(solana.results[0]?.balances.keys() || ['native']);
    const rows: string[] = [];
    rows.push(['Address', ...solana.tokenHeaders].join(this.DELIMITER));

    for (const walletResult of solana.results) {
      const balances = tokenKeys.map((token) => walletResult.balances.get(token) || '0');
      rows.push([walletResult.wallet.address, ...balances].join(this.DELIMITER));
    }

    fs.writeFileSync(fullPath, `${rows.join('\n')}\n`, 'utf-8');
  }
}

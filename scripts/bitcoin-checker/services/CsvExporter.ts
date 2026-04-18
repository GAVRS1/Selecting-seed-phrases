import fs from 'fs';
import path from 'path';
import SharedPaths from '../../checker-shared/paths.ts';
import { AllNetworksCheckResult, WalletBalance } from '../types';

export class CsvExporter {
  private readonly DELIMITER = ';';
  private readonly RESULTS_FOLDER = SharedPaths.ROOT_RESULT_DIR;

  async exportSingleNetwork(data: WalletBalance[], filename = 'bitcoin.csv'): Promise<void> {
    SharedPaths.ensureRootDirectories();
    const fullPath = path.join(this.RESULTS_FOLDER, filename);
    const rows: string[] = [];

    rows.push(['Address', 'BTC'].join(this.DELIMITER));

    for (const row of data) {
      rows.push([row.wallet.address, row.balances.get('native') || '0'].join(this.DELIMITER));
    }

    fs.writeFileSync(fullPath, `${rows.join('\n')}\n`, 'utf-8');
  }

  async exportAllNetworks(allResults: AllNetworksCheckResult[], filename = 'bitcoin_all_networks_balances.csv'): Promise<void> {
    const bitcoin = allResults[0];
    if (!bitcoin) return;
    await this.exportSingleNetwork(bitcoin.results, filename);
  }
}

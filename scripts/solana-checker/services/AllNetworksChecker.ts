import { AllNetworksCheckResult, Network, WalletData } from '../types';
import { ConfigManager } from './ConfigManager';
import { UIService } from './UIService';
import { WalletChecker } from './WalletChecker';

export class AllNetworksChecker {
  constructor(
    private readonly wallets: WalletData[],
    private readonly configManager: ConfigManager,
    private readonly ui: UIService
  ) {}

  async checkAllNetworks(): Promise<AllNetworksCheckResult[]> {
    this.ui.showInfo('Режим мультичекера: проверка всех доступных сетей (сейчас только Solana).');
    const network = Network.SOLANA;
    const cfg = this.configManager.getNetworkConfig(network);
    const appConfig = this.configManager.getAppConfig();

    const checker = new WalletChecker(
      {
        network,
        wallets: this.wallets,
        tokens: cfg.tokens,
        options: {
          showProgress: appConfig.enableProgressBar,
          logErrors: appConfig.enableLogging,
          batchSize: appConfig.defaultBatchSize,
          retryAttempts: appConfig.defaultRetryAttempts,
          retryDelay: appConfig.defaultRetryDelay,
        },
      },
      cfg.rpcUrl
    );

    const results = await checker.check();
    return [{ network, results, tokenHeaders: checker.getTokenHeaders() }];
  }
}

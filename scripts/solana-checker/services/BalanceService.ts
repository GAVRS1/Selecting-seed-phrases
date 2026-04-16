import { Connection, LAMPORTS_PER_SOL, PublicKey } from '@solana/web3.js';
import { getAssociatedTokenAddressSync } from '@solana/spl-token';

export interface BalanceProvider {
  getBatchBalances(addresses: string[]): Promise<Map<string, string>>;
}

export class SolNativeBalanceProvider implements BalanceProvider {
  constructor(private readonly connection: Connection) {}

  async getBatchBalances(addresses: string[]): Promise<Map<string, string>> {
    const keys = addresses.map((address) => new PublicKey(address));
    const accounts = await this.connection.getMultipleAccountsInfo(keys);
    const balances = new Map<string, string>();

    addresses.forEach((address, idx) => {
      const lamports = accounts[idx]?.lamports ?? 0;
      balances.set(address, (lamports / LAMPORTS_PER_SOL).toString());
    });

    return balances;
  }
}

export class SplTokenBalanceProvider implements BalanceProvider {
  private mint: PublicKey;

  constructor(private readonly connection: Connection, mintAddress: string) {
    this.mint = new PublicKey(mintAddress);
  }

  async getBatchBalances(addresses: string[]): Promise<Map<string, string>> {
    const ownerKeys = addresses.map((address) => new PublicKey(address));
    const ataKeys = ownerKeys.map((owner) => getAssociatedTokenAddressSync(this.mint, owner, true));

    const tokenAccounts = await this.connection.getMultipleParsedAccounts(ataKeys);
    const balances = new Map<string, string>();

    addresses.forEach((address, idx) => {
      const parsed = tokenAccounts.value[idx]?.data;
      if (!parsed || !('parsed' in parsed)) {
        balances.set(address, '0');
        return;
      }

      const amount = (parsed as any).parsed?.info?.tokenAmount?.uiAmountString ?? '0';
      balances.set(address, amount);
    });

    return balances;
  }
}

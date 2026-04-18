import { Network } from '../../scripts/solana-checker/types.ts';

export const NETWORKS_CONFIG = {
  [Network.SOLANA]: {
    RPC_URL: 'https://api.mainnet-beta.solana.com',
    NAME: 'Solana',
    NATIVE_CURRENCY: 'SOL',
    TOKENS: {
      USDC: 'EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v',
      USDT: 'Es9vMFrzaCERmJfrF4H2FYDut4S6j6D8S4x2fQ6NfV8'
    }
  }
};

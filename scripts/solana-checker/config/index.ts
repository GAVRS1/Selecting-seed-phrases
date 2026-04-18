import { Config, Network } from '../types';
import * as networksModule from '../../../config/checkers/solana-networks';

const NETWORKS_CONFIG =
  (networksModule as any).NETWORKS_CONFIG ??
  (networksModule as any).default?.NETWORKS_CONFIG ??
  (networksModule as any).default;


export const CONFIG: Config = Object.entries(NETWORKS_CONFIG).reduce((acc, [network, config]) => {
  acc[network as Network] = {
    RPC_URL: config.RPC_URL,
    COLUMNS: ['native', ...Object.values(config.TOKENS)],
    TOKENS: config.TOKENS,
    NAME: config.NAME,
    NATIVE_CURRENCY: config.NATIVE_CURRENCY,
  };
  return acc;
}, {} as Config);

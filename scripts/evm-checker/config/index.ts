import { Config, Network } from "../types";
import * as networksModule from '../../../config/checkers/evm-networks';

const NETWORKS_CONFIG =
  (networksModule as any).NETWORKS_CONFIG ??
  (networksModule as any).default?.NETWORKS_CONFIG ??
  (networksModule as any).default;


// Преобразуем конфигурацию в формат, ожидаемый приложением
export const CONFIG: Config = Object.entries(NETWORKS_CONFIG).reduce((acc, [network, config]) => {
  acc[network as Network] = {
    RPC_URL: config.RPC_URL,
    COLUMNS: ["native", ...Object.values(config.TOKENS)],
    TOKENS: config.TOKENS,
    NAME: config.NAME,
    NATIVE_CURRENCY: config.NATIVE_CURRENCY,
    ...((config as any).MULTICALL3_CONTRACT && { MULTICALL3_CONTRACT: (config as any).MULTICALL3_CONTRACT }),
  };
  return acc;
}, {} as Config);

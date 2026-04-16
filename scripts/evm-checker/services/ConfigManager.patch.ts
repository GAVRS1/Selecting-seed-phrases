// Добавь в AppConfig
postgresConnectionString?: string;
postgresSourceTable: string;
dbFetchLimit: number;
deleteProcessedWallets: boolean;

// Значения по умолчанию в getDefaultConfig()
postgresConnectionString: process.env.POSTGRES_CONN,
postgresSourceTable: 'recovered_wallets_evm',
dbFetchLimit: 500,
deleteProcessedWallets: true,

// Добавь в applyEnvironmentVariables(envVars)
if (envVars.POSTGRES_CONN) {
  this.config.postgresConnectionString = envVars.POSTGRES_CONN;
}
if (envVars.POSTGRES_SOURCE_TABLE) {
  this.config.postgresSourceTable = envVars.POSTGRES_SOURCE_TABLE;
}
if (envVars.DB_FETCH_LIMIT) {
  this.config.dbFetchLimit = parseInt(envVars.DB_FETCH_LIMIT) || this.config.dbFetchLimit;
}
if (envVars.DELETE_PROCESSED_WALLETS) {
  this.config.deleteProcessedWallets = envVars.DELETE_PROCESSED_WALLETS.toLowerCase() === 'true';
}

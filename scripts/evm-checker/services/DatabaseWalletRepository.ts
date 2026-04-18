import { Pool } from 'pg';
import { WalletData } from '../types';

export interface DbWalletRow extends WalletData {
  id: number;
}

export interface DatabaseConfig {
  connectionString: string;
  sourceTable: string;
  fetchLimit: number;
}

export class DatabaseWalletRepository {
  private readonly pool: Pool;
  private readonly table: string;

  constructor(private readonly config: DatabaseConfig) {
    this.table = this.validateIdentifier(config.sourceTable);
    this.pool = new Pool({ connectionString: config.connectionString });
  }

  async fetchBatch(lastProcessedId?: number): Promise<DbWalletRow[]> {
    const hasPositiveLimit = Number.isFinite(this.config.fetchLimit) && this.config.fetchLimit > 0;
    const hasCursor = typeof lastProcessedId === 'number';
    const useLimit = hasPositiveLimit;

    const whereClause = hasCursor ? 'WHERE id > $1' : '';
    const limitClause = useLimit ? `LIMIT $${hasCursor ? 2 : 1}` : '';
    const sql = `
      SELECT id, address, mnemonic
      FROM ${this.table}
      ${whereClause}
      ORDER BY id
      ${limitClause}
    `;

    const queryParams: Array<number> = [];
    if (hasCursor) {
      queryParams.push(lastProcessedId);
    }
    if (useLimit) {
      queryParams.push(this.config.fetchLimit);
    }

    const result = await this.pool.query(sql, queryParams);

    return result.rows.map((row) => ({
      id: Number(row.id),
      address: String(row.address),
      mnemonic: row.mnemonic ? String(row.mnemonic) : undefined,
    }));
  }

  async deleteBatch(ids: number[]): Promise<number> {
    if (ids.length === 0) {
      return 0;
    }

    const sql = `DELETE FROM ${this.table} WHERE id = ANY($1::bigint[])`;
    const result = await this.pool.query(sql, [ids]);
    return result.rowCount ?? 0;
  }

  async close(): Promise<void> {
    await this.pool.end();
  }

  private validateIdentifier(name: string): string {
    if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(name)) {
      throw new Error(`Некорректное имя таблицы: ${name}`);
    }
    return name;
  }
}

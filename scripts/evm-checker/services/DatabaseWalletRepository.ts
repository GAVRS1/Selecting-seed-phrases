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

  async fetchBatch(): Promise<DbWalletRow[]> {
    const sql = `
      SELECT id, address
      FROM ${this.table}
      ORDER BY id
      LIMIT $1
    `;

    const result = await this.pool.query(sql, [this.config.fetchLimit]);

    return result.rows.map((row) => ({
      id: Number(row.id),
      address: String(row.address),
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
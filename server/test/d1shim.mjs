// A stand-in for Cloudflare D1 over Node's built-in SQLite, for tests and local runs.
import { DatabaseSync } from 'node:sqlite';
import { readFileSync, readdirSync } from 'node:fs';
import { join } from 'node:path';

class Stmt {
  constructor(db, sql, args = []) { this.db = db; this.sql = sql; this.args = args; }
  bind(...args) { return new Stmt(this.db, this.sql, args.map((a) => (a === undefined ? null : typeof a === 'boolean' ? (a ? 1 : 0) : a))); }
  _plain(row) { return row ? { ...row } : null; }
  async first() { return this._plain(this.db.prepare(this.sql).get(...this.args)); }
  async all() { return { results: this.db.prepare(this.sql).all(...this.args).map((r) => ({ ...r })), success: true }; }
  async run() { const r = this.db.prepare(this.sql).run(...this.args); return { success: true, meta: { changes: Number(r.changes), last_row_id: Number(r.lastInsertRowid) } }; }
  _sync() { return this.db.prepare(this.sql).all(...this.args); }
}

export function makeD1(file = ':memory:', migrationsDir) {
  const db = new DatabaseSync(file);
  if (migrationsDir) {
    const have = db.prepare("SELECT name FROM sqlite_master WHERE type='table' AND name='players'").get();
    if (!have) for (const f of readdirSync(migrationsDir).filter((x) => x.endsWith('.sql')).sort()) db.exec(readFileSync(join(migrationsDir, f), 'utf8'));
  }
  return {
    raw: db,
    prepare: (sql) => new Stmt(db, sql),
    async batch(stmts) {
      db.exec('BEGIN');
      try {
        const out = stmts.map((s) => ({ results: s._sync().map((r) => ({ ...r })), success: true }));
        db.exec('COMMIT');
        return out;
      } catch (e) { db.exec('ROLLBACK'); throw e; }
    },
  };
}

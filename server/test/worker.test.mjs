// The Worker end to end, with SQLite standing in for D1.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { gunzipSync } from 'node:zlib';
import worker, { cleanName } from '../src/index.js';
import { makeD1 } from './d1shim.mjs';
import { toBase64, fromBase64 } from '../src/replay.js';

const fx = (n) => new Uint8Array(gunzipSync(readFileSync(new URL(`./fixtures/${n}.alr.gz`, import.meta.url))));
const migrations = new URL('../migrations/', import.meta.url).pathname;
const fresh = () => ({ DB: makeD1(':memory:', migrations) });

async function call(env, method, path, body, ip = '1.2.3.4') {
  const res = await worker.fetch(new Request('http://x' + path, {
    method, body: body ? JSON.stringify(body) : undefined, headers: { 'cf-connecting-ip': ip, 'content-type': 'application/json' },
  }), env);
  const text = await res.text();
  let json = null;
  try { json = JSON.parse(text); } catch {}
  return { status: res.status, json, text };
}
const post = (env, id, name, bytes, key, ip) =>
  call(env, 'POST', '/api/runs', { player: { platform: 'Steam', id, name }, key, replay: toBase64(bytes) }, ip);
const rewind = (env, id) => env.DB.raw.prepare('UPDATE players SET last_upload = last_upload - 100 WHERE id = ?').run('Steam:' + id);

test('first post: accepted, ranked, with a key for this install', async () => {
  const env = fresh();
  const r = await post(env, '7656119800001', 'Pilot', fx('0-4-1-0'));
  assert.equal(r.status, 200, r.text);
  assert.equal(r.json.ok, true);
  assert.equal(r.json.best, true);
  assert.equal(r.json.pos, 1);
  assert.equal(r.json.of, 1);
  assert.match(r.json.key, /^[0-9a-f]{48}$/);
  assert.match(r.json.pid, /^[0-9a-f]{16}$/);
  const board = await call(env, 'GET', '/api/boards/0-4-1-0');
  assert.equal(board.json.entries.length, 1);
  assert.equal(board.json.entries[0].name, 'Pilot');
  assert.equal(board.json.title, 'Flick Mixed · Medium');
  // the replay comes back byte for byte
  const rp = await call(env, 'GET', `/api/replays/${r.json.run}`);
  assert.deepEqual(fromBase64(rp.json.replay), fx('0-4-1-0'));
});

test('the plugin text format', async () => {
  const env = fresh();
  const r = await call(env, 'POST', '/api/runs?format=text', { player: { platform: 'Epic', id: 'abcdef0123456789', name: 'Tab\tName' }, replay: toBase64(fx('2-0-1-0')) });
  assert.equal(r.status, 200);
  assert.match(r.text, /^ok 1$/m);
  assert.match(r.text, /^best 1$/m);
  assert.match(r.text, /^pos 1$/m);
  const pid = /^pid (\w+)$/m.exec(r.text)[1];
  const b = await call(env, 'GET', `/api/boards/2-0-1-0?format=text&me=${pid}`);
  assert.match(b.text, /^total 1$/m);
  assert.match(b.text, /^me 1\t43777\t\d+\t\d{4}-\d\d-\d\d$/m);
  const row = /^row (.*)$/m.exec(b.text)[1].split('\t');
  assert.equal(row[0], '1');
  assert.equal(row[2], 'TabName');
  assert.equal(row[3], 'Epic');
  assert.equal(row[4], '43777');
  const rp = await call(env, 'GET', `/api/replays/${row[6]}?format=text`);
  assert.deepEqual(fromBase64(/^replay (.*)$/m.exec(rp.text)[1]), fx('2-0-1-0'));
});

test('another install can not post as you; you can, with your key', async () => {
  const env = fresh();
  const first = await post(env, '111', 'A', fx('0-4-1-0'));
  rewind(env, '111');
  const thief = await post(env, '111', 'A', fx('0-4-1-0-better'), 'wrong');
  assert.equal(thief.status, 401);
  const me = await post(env, '111', 'A', fx('0-4-1-0-better'), first.json.key);
  assert.equal(me.status, 200, me.text);
  assert.equal(me.json.best, true);
  // the old best's replay is gone, the new one is kept
  assert.equal((await call(env, 'GET', `/api/replays/${first.json.run}`)).status, 404);
  assert.equal((await call(env, 'GET', `/api/replays/${me.json.run}`)).status, 200);
  const board = await call(env, 'GET', '/api/boards/0-4-1-0');
  assert.equal(board.json.entries.length, 1);
  assert.equal(board.json.entries[0].score, 44699);
});

test('too soon, and not a new best', async () => {
  const env = fresh();
  const first = await post(env, '222', 'B', fx('0-4-1-0-better'));
  const soon = await post(env, '222', 'B', fx('0-4-1-0'), first.json.key);
  assert.equal(soon.status, 429);
  rewind(env, '222');
  const lower = await post(env, '222', 'B', fx('0-4-1-0'), first.json.key);
  assert.equal(lower.status, 200);
  assert.equal(lower.json.best, false);
  assert.equal(lower.json.score, 44699);
});

test('every preset run goes up with the score it claims (the replayed score is only kept for reference)', async () => {
  const env = fresh();
  const b = new Uint8Array(fx('0-4-1-0'));
  new DataView(b.buffer).setInt32(40, 50000, true);
  const r = await post(env, '333-raised', 'C', b);
  assert.equal(r.status, 200, r.text);
  assert.equal(r.json.score, 50000);
  const e = (await call(env, 'GET', '/api/boards/0-4-1-0')).json.entries[0];
  assert.equal(e.score, 50000);
  assert.equal(e.flags, undefined); // nothing shown
  const row = env.DB.raw.prepare('SELECT flags FROM runs WHERE id = ?').get(r.json.run);
  assert.match(row.flags, /replayed:17019/);
});

test('a run that is not the preset it says is turned away, and kept (without the player) to look at', async () => {
  const env = fresh();
  const b = new Uint8Array(fx('0-4-1-0'));
  new DataView(b.buffer).setFloat32(24, 450, true); // ball distance no preset uses
  const r = await post(env, '333-wrong', 'C', b);
  assert.equal(r.status, 422);
  assert.match(r.json.error, /distance/);
  const list = await call(env, 'GET', '/api/rejects');
  assert.equal(list.json.rejects.length, 1);
  assert.equal(JSON.stringify(list.json).includes('333'), false); // no player id
  const one = await call(env, 'GET', `/api/rejects/${list.json.rejects[0].id}`);
  assert.deepEqual(fromBase64(one.json.replay), b);
});

test('ranking order, ties, and your own place', async () => {
  const env = fresh();
  await post(env, 'id-p1', 'Low', fx('0-4-1-0'), undefined, '10.0.0.1');
  const hi = await post(env, 'id-p2', 'High', fx('0-4-1-0-better'), undefined, '10.0.0.2');
  assert.equal(hi.json.pos, 1);
  assert.equal(hi.json.of, 2);
  const tie = await post(env, 'id-p3', 'Tie', fx('0-4-1-0'), undefined, '10.0.0.3');
  assert.equal(tie.json.pos, 3); // same score as Low, who got there first
  const b = await call(env, 'GET', '/api/boards/0-4-1-0');
  assert.deepEqual(b.json.entries.map((e) => e.name), ['High', 'Low', 'Tie']);
  const meText = await call(env, 'GET', `/api/boards/0-4-1-0?format=text&limit=1&me=${tie.json.pid}`);
  assert.match(meText.text, /^me 3\t/m);
});

test('new players per network are limited', async () => {
  const env = fresh();
  for (let i = 0; i < 20; i++) assert.equal((await post(env, 'id-n' + i, 'N', fx('3-1-1-1'), undefined, '9.9.9.9')).status, 200);
  assert.equal((await post(env, 'id-extra', 'N', fx('3-1-1-1'), undefined, '9.9.9.9')).status, 429);
});

test('banned players and hidden runs drop off the board', async () => {
  const env = fresh();
  const a = await post(env, 'id-b1', 'Banned', fx('1-1-1-0'), undefined, '8.0.0.1');
  const b = await post(env, 'id-b2', 'Hidden', fx('1-1-1-0'), undefined, '8.0.0.2');
  await post(env, 'id-b3', 'Fine', fx('1-1-1-0'), undefined, '8.0.0.3');
  env.DB.raw.prepare("UPDATE players SET banned = 1 WHERE id = 'Steam:id-b1'").run();
  env.DB.raw.prepare('UPDATE runs SET hidden = 1 WHERE id = ?').run(b.json.run);
  const board = await call(env, 'GET', '/api/boards/1-1-1-0');
  assert.deepEqual(board.json.entries.map((e) => e.name), ['Fine']);
  assert.equal(board.json.total, 1);
  assert.equal((await post(env, 'id-b1', 'Banned', fx('1-1-1-0'), a.json.key, '8.0.0.1')).status, 403);
  assert.equal((await call(env, 'GET', `/api/replays/${a.json.run}`)).status, 404);
});

test('old plugin versions are asked to update', async () => {
  const env = fresh();
  const b = new Uint8Array(fx('0-4-1-0'));
  b.set(new TextEncoder().encode('2.5.0\0\0\0'), 52);
  const r = await post(env, 'id-v1', 'Old', b);
  assert.equal(r.status, 426);
});

test('board list, unknown boards, the web page', async () => {
  const env = fresh();
  await post(env, 'id-l1', 'L', fx('3-3-0-0'));
  const list = await call(env, 'GET', '/api/boards');
  const row = list.json.boards.find((x) => x.board === '3-3-0-0');
  assert.equal(row.players, 1);
  assert.equal(row.top, 43140);
  assert.ok(list.json.boards.length > 100);
  assert.equal((await call(env, 'GET', '/api/boards/0-5-1-0')).status, 404); // Flip Reset has no ranges
  assert.equal((await call(env, 'GET', '/api/boards/0-5-0-1')).status, 404); // nor No air roll
  const page = await call(env, 'GET', '/');
  assert.equal(page.status, 200);
  assert.match(page.text, /AirLabs Leaderboard/);
});

test('names are cleaned up', () => {
  assert.equal(cleanName('  Pilot  '), 'Pilot');
  assert.equal(cleanName('a\u0000b‮c'), 'abc');
  assert.equal(cleanName(''), 'Player');
  assert.equal([...cleanName('x'.repeat(50))].length, 32);
  assert.equal(cleanName('Zoë 🚀'), 'Zoë 🚀');
});

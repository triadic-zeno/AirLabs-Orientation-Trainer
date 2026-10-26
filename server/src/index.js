// AirLabs leaderboard: a Cloudflare Worker with a D1 database.
//
//   POST /api/runs              a finished preset run from the plugin (player + replay)
//   GET  /api/boards            every board with its player count and leader
//   GET  /api/boards/:board     one board (?limit=, ?me=<public id>)
//   GET  /api/replays/:run      the replay of a run that is somebody's current best
//   GET  /api/rejects[/:id]     recent runs the checks turned away (reason, board, score, replay; no player)
//   GET  /                      the leaderboard as a web page
//
// Plugin requests add ?format=text and get "key value" lines (rows tab-separated), so the
// plugin needs no JSON parser. Everything else is JSON.
import { boardKey, boardTitle, parseBoard, allBoards, tierIndex, DATA } from './core.js';
import { parseReplay, fromBase64, toBase64, deflate, inflate, packReplay, unpackReplay } from './replay.js';
import { verifyReplay } from './verify.js';
import { PAGE } from './page.js';

const MIN_VERSION = [2, 6, 0];
const UPLOAD_GAP_SEC = 45;     // a run takes 60 s, so a real player can't post faster than this
const SIGNUPS_PER_DAY = 20;    // new players per network address per day (shared addresses are common)
const MAX_BODY = 400 * 1024;

export default {
  async fetch(req, env) {
    const url = new URL(req.url);
    const text = url.searchParams.get('format') === 'text';
    try {
      if (req.method === 'OPTIONS') return new Response(null, { headers: cors() });
      const p = url.pathname.replace(/\/+$/, '') || '/';
      if (req.method === 'GET' && p === '/') return new Response(PAGE, { headers: { 'content-type': 'text/html; charset=utf-8', 'cache-control': 'max-age=300' } });
      if (req.method === 'GET' && p === '/api/health') return reply({ ok: true }, text);
      if (req.method === 'POST' && p === '/api/runs') return await postRun(req, env, text);
      if (req.method === 'GET' && p === '/api/boards') return await listBoards(env, text);
      let m;
      if (req.method === 'GET' && (m = /^\/api\/boards\/([\w-]+)$/.exec(p))) return await getBoard(env, m[1], url, text);
      if (req.method === 'GET' && (m = /^\/api\/replays\/(\d+)$/.exec(p))) return await getReplay(env, +m[1], text);
      if (req.method === 'GET' && p === '/api/rejects') return await listRejects(env, url, text);
      if (req.method === 'GET' && (m = /^\/api\/rejects\/(\d+)$/.exec(p))) return await getReject(env, +m[1], text);
      return reply({ ok: false, error: 'not found' }, text, 404);
    } catch (e) {
      console.log('error', e && e.stack || e);
      return reply({ ok: false, error: 'server error' }, text, 500);
    }
  },
};

// ---------------------------------------------------------------------------------------------
// replies
// ---------------------------------------------------------------------------------------------
function cors() {
  return { 'access-control-allow-origin': '*', 'access-control-allow-methods': 'GET, POST, OPTIONS', 'access-control-allow-headers': 'content-type' };
}
// JSON, or for the plugin: one "key value" per line, arrays as repeated "row" lines with tabs.
function reply(obj, text, status = 200, extra = {}) {
  if (!text) return new Response(JSON.stringify(obj), { status, headers: { 'content-type': 'application/json', ...cors(), ...extra } });
  const lines = [];
  for (const [k, v] of Object.entries(obj)) {
    if (v === null || v === undefined) continue;
    if (Array.isArray(v)) for (const row of v) lines.push('row ' + row.join('\t'));
    else if (typeof v === 'object') lines.push(k + ' ' + Object.values(v).map(clean).join('\t'));
    else lines.push(k + ' ' + (typeof v === 'boolean' ? (v ? 1 : 0) : clean(v)));
  }
  return new Response(lines.join('\n') + '\n', { status, headers: { 'content-type': 'text/plain; charset=utf-8', ...cors(), ...extra } });
}
const clean = (v) => String(v ?? '').replace(/[\t\r\n]/g, ' ');
const day = (sec) => new Date(sec * 1000).toISOString().slice(0, 10);
const now = () => Math.floor(Date.now() / 1000);

async function sha256hex(s) {
  const d = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(s));
  return [...new Uint8Array(d)].map((b) => b.toString(16).padStart(2, '0')).join('');
}
const publicId = async (id) => (await sha256hex('airlabs:' + id)).slice(0, 16);

// A Rocket League name, made safe to show: no control characters, at most 32 characters.
export function cleanName(s) {
  const t = String(s || '').normalize('NFC').replace(/[\p{C}\t\r\n]/gu, '').replace(/\s+/gu, ' ').trim();
  return [...t].slice(0, 32).join('') || 'Player';
}
function versionOk(v) {
  const p = String(v).split('.').map((x) => parseInt(x, 10) || 0);
  for (let i = 0; i < 3; i++) { if ((p[i] || 0) !== MIN_VERSION[i]) return (p[i] || 0) > MIN_VERSION[i]; }
  return true;
}

// ---------------------------------------------------------------------------------------------
// POST /api/runs  {"player": {"platform", "id", "name"}, "key": "...", "replay": "<base64 ALR1>"}
// ---------------------------------------------------------------------------------------------
async function postRun(req, env, text) {
  const bad = (status, error) => reply({ ok: false, error }, text, status);
  const bodyText = await req.text();
  if (bodyText.length > MAX_BODY) return bad(413, 'upload too large');
  let body;
  try { body = JSON.parse(bodyText); } catch { return bad(400, 'bad request'); }
  const pl = body && body.player;
  if (!pl || typeof pl.id !== 'string' || typeof pl.platform !== 'string') return bad(400, 'missing player');
  const platform = pl.platform.replace(/[^A-Za-z0-9]/g, '').slice(0, 16) || 'Unknown';
  const rawId = pl.id.replace(/[^A-Za-z0-9|_-]/g, '').slice(0, 80);
  if (rawId.length < 3 || /^0+$/.test(rawId)) return bad(400, 'missing player id');
  const id = platform + ':' + rawId;
  const name = cleanName(pl.name);

  let rep, raw;
  try { raw = fromBase64(body.replay || ''); rep = parseReplay(raw); } catch (e) { return bad(400, e.message || 'bad replay'); }
  if (!versionOk(rep.version)) return bad(426, 'update AirLabs to post runs');
  const b = parseBoard(boardKey(rep.mode, rep.preset, rep.range, (rep.flags & 1) !== 0));
  if (!b) return bad(400, 'not a preset run');
  const board = boardKey(b.mode, b.preset, b.range, b.noRoll);
  const t = now();

  // ---- the player: first upload hands out a key; later uploads must bring it ----
  let player = await env.DB.prepare('SELECT * FROM players WHERE id = ?').bind(id).first();
  let newKey = null;
  if (player) {
    if (player.banned) return bad(403, 'this account is not allowed on the leaderboard');
    if (player.key_hash) {
      if (typeof body.key !== 'string' || (await sha256hex(body.key)) !== player.key_hash)
        return bad(401, 'this Rocket League account is linked to another AirLabs install');
    } else newKey = randomKey();
    if (t - player.last_upload < UPLOAD_GAP_SEC) return bad(429, 'too many uploads, slow down');
  } else {
    const ip = req.headers.get('cf-connecting-ip') || 'local';
    const d = Math.floor(t / 86400);
    const su = await env.DB.prepare('SELECT n FROM signups WHERE ip = ? AND day = ?').bind(ip, d).first();
    if (su && su.n >= SIGNUPS_PER_DAY) return bad(429, 'too many new players from this network today');
    await env.DB.prepare('INSERT INTO signups (ip, day, n) VALUES (?, ?, 1) ON CONFLICT(ip, day) DO UPDATE SET n = n + 1').bind(ip, d).run();
    newKey = randomKey();
  }
  const pid = player ? player.pid : await publicId(id);
  const keyHash = newKey ? await sha256hex(newKey) : player.key_hash;
  if (player) {
    await env.DB.prepare('UPDATE players SET name = ?, last_upload = ?, key_hash = ? WHERE id = ?').bind(name, t, keyHash, id).run();
  } else {
    await env.DB.prepare('INSERT INTO players (id, pid, name, platform, key_hash, created, last_upload) VALUES (?, ?, ?, ?, ?, ?, ?)')
      .bind(id, pid, name, platform, keyHash, t, t).run();
  }

  // ---- only a new personal best is checked and stored ----
  const prev = await env.DB.prepare('SELECT run, score FROM best WHERE board = ? AND player = ?').bind(board, id).first();
  if (prev && rep.score <= prev.score) {
    await env.DB.prepare('UPDATE best SET plays = plays + 1 WHERE board = ? AND player = ?').bind(board, id).run();
    const pos = await position(env, board, prev.score, null, prev.run, pid);
    return reply({ ok: true, best: false, board, title: boardTitle(b), score: prev.score, pos: pos.pos, of: pos.total, pid, key: newKey,
      message: `Not a new best. Your best ${prev.score.toLocaleString('en-US')} is #${pos.pos} of ${pos.total}.` }, text);
  }

  const v = verifyReplay(rep);
  if (!v.ok) {
    console.log('rejected', board, rep.score, v.reason);
    // Keep it (without who sent it) so the checks can be looked at against real runs.
    await env.DB.batch([
      env.DB.prepare('INSERT INTO rejects (created, board, score, version, reason, data) VALUES (?, ?, ?, ?, ?, ?)')
        .bind(t, board, rep.score, rep.version, v.reason, toBase64(await deflate(packReplay(raw)))),
      env.DB.prepare('DELETE FROM rejects WHERE id <= (SELECT MAX(id) - 300 FROM rejects)'),
    ]);
    return reply({ ok: false, error: 'run not accepted: ' + v.reason, pid, key: newKey }, text, 422);
  }

  const stored = toBase64(await deflate(packReplay(raw)));
  const ins = await env.DB.prepare(
    'INSERT INTO runs (player, board, score, created, version, targets, misses, on_pct, avg_time, flags) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?) RETURNING id')
    .bind(id, board, rep.score, t, rep.version, v.stats.targets, v.stats.misses, v.stats.onPct, v.stats.avgTime, v.flags.join(' ')).first();
  const runId = ins.id;
  const ops = [
    env.DB.prepare('INSERT INTO replays (run, data) VALUES (?, ?)').bind(runId, stored),
    env.DB.prepare(`INSERT INTO best (board, player, run, score, created) VALUES (?, ?, ?, ?, ?)
      ON CONFLICT(board, player) DO UPDATE SET run = excluded.run, score = excluded.score, created = excluded.created, plays = plays + 1`)
      .bind(board, id, runId, rep.score, t),
  ];
  if (prev) ops.push(env.DB.prepare('DELETE FROM replays WHERE run = ?').bind(prev.run)); // only the best keeps its replay
  await env.DB.batch(ops);
  const pos = await position(env, board, rep.score, t, runId, pid);
  return reply({
    ok: true, best: true, board, title: boardTitle(b), score: rep.score, pos: pos.pos, of: pos.total, run: runId, pid, key: newKey,
    message: `New best posted: #${pos.pos} of ${pos.total}.`,
  }, text);
}

function randomKey() {
  const a = new Uint8Array(24);
  crypto.getRandomValues(a);
  return [...a].map((x) => x.toString(16).padStart(2, '0')).join('');
}

// Your place on a board (1 = top). Ties go to whoever got there first.
async function position(env, board, score, created, run, pid) {
  if (created === null) {
    const me = await env.DB.prepare('SELECT b.created FROM best b JOIN players p ON p.id = b.player WHERE b.board = ? AND p.pid = ?').bind(board, pid).first();
    created = me ? me.created : now();
  }
  const r = await env.DB.prepare(`SELECT
      (SELECT COUNT(*) FROM best b JOIN players p ON p.id = b.player JOIN runs r ON r.id = b.run
        WHERE b.board = ?1 AND p.banned = 0 AND r.hidden = 0
          AND (b.score > ?2 OR (b.score = ?2 AND (b.created < ?3 OR (b.created = ?3 AND b.run < ?4))))) AS better,
      (SELECT COUNT(*) FROM best b JOIN players p ON p.id = b.player JOIN runs r ON r.id = b.run
        WHERE b.board = ?1 AND p.banned = 0 AND r.hidden = 0) AS total`).bind(board, score, created, run).first();
  return { pos: r.better + 1, total: r.total };
}

// ---------------------------------------------------------------------------------------------
// GET /api/boards, /api/boards/:board, /api/replays/:run
// ---------------------------------------------------------------------------------------------
async function listBoards(env, text) {
  const { results } = await env.DB.prepare(`SELECT b.board, COUNT(*) AS players, MAX(b.score) AS top
      FROM best b JOIN players p ON p.id = b.player JOIN runs r ON r.id = b.run
      WHERE p.banned = 0 AND r.hidden = 0 GROUP BY b.board`).all();
  const counts = Object.fromEntries(results.map((x) => [x.board, x]));
  const boards = allBoards().map((k) => {
    const b = parseBoard(k);
    return { board: k, title: boardTitle(b), mode: b.mode, preset: b.preset, range: b.range, noRoll: b.noRoll,
      players: counts[k] ? counts[k].players : 0, top: counts[k] ? counts[k].top : null };
  });
  if (text) return reply({ ok: true, rows: boards.filter((x) => x.players).map((x) => [x.board, x.players, x.top]) }, true);
  return reply({ ok: true, modes: DATA.modes, ranges: DATA.ranges, tiers: DATA.tierShort,
    presets: [DATA.flick, DATA.track, DATA.hybrid, DATA.chase].map((l) => l.map((p) => (p.group ? p.group + ' ' : '') + p.name)), boards }, false,
    200, { 'cache-control': 'max-age=30' });
}

async function getBoard(env, key, url, text) {
  const b = parseBoard(key);
  if (!b) return reply({ ok: false, error: 'no such board' }, text, 404);
  const limit = Math.min(200, Math.max(1, parseInt(url.searchParams.get('limit') || '100', 10) || 100));
  const me = (url.searchParams.get('me') || '').replace(/[^0-9a-f]/g, '').slice(0, 16);
  const { results } = await env.DB.prepare(`SELECT b.score, b.created, b.run, p.pid, p.name, p.platform, r.targets, r.misses, r.on_pct, r.avg_time
      FROM best b JOIN players p ON p.id = b.player JOIN runs r ON r.id = b.run
      WHERE b.board = ? AND p.banned = 0 AND r.hidden = 0
      ORDER BY b.score DESC, b.created ASC, b.run ASC LIMIT ?`).bind(key, limit).all();
  const total = (await env.DB.prepare(`SELECT COUNT(*) AS n FROM best b JOIN players p ON p.id = b.player JOIN runs r ON r.id = b.run
      WHERE b.board = ? AND p.banned = 0 AND r.hidden = 0`).bind(key).first()).n;
  let mine = null;
  if (me) {
    const row = await env.DB.prepare(`SELECT b.score, b.created, b.run FROM best b JOIN players p ON p.id = b.player JOIN runs r ON r.id = b.run
        WHERE b.board = ? AND p.pid = ? AND p.banned = 0 AND r.hidden = 0`).bind(key, me).first();
    if (row) mine = { pos: (await position(env, key, row.score, row.created, row.run, me)).pos, score: row.score, run: row.run, date: day(row.created) };
  }
  const entries = results.map((x, i) => ({
    pos: i + 1, pid: x.pid, name: x.name, platform: x.platform, score: x.score, tier: tierIndex(x.score, b.mode, b.preset, b.range),
    date: day(x.created), run: x.run, targets: x.targets, misses: x.misses, onPct: x.on_pct, avgTime: x.avg_time,
  }));
  if (text) {
    return reply({ ok: true, board: key, title: boardTitle(b), total, me: mine,
      rows: entries.map((e) => [e.pos, e.pid, cleanText(e.name), e.platform, e.score, e.date, e.run]) }, true, 200, { 'cache-control': 'max-age=10' });
  }
  return reply({ ok: true, board: key, title: boardTitle(b), total, me: mine, entries }, false, 200, { 'cache-control': 'max-age=10' });
}
const cleanText = (s) => String(s).replace(/[\t\r\n]/g, ' ');

async function getReplay(env, runId, text) {
  const row = await env.DB.prepare(`SELECT r.id, r.board, r.score, r.created, r.flags, p.name, p.platform, p.pid, rp.data
      FROM runs r JOIN players p ON p.id = r.player LEFT JOIN replays rp ON rp.run = r.id
      WHERE r.id = ? AND r.hidden = 0 AND p.banned = 0`).bind(runId).first();
  if (!row) return reply({ ok: false, error: 'no such run' }, text, 404);
  if (!row.data) return reply({ ok: false, error: "that run isn't this player's best any more, so its replay wasn't kept" }, text, 404);
  const bytes = unpackReplay(await inflate(fromBase64(row.data)));
  const b = parseBoard(row.board);
  return reply({ ok: true, run: row.id, board: row.board, title: boardTitle(b), name: row.name, platform: row.platform, pid: row.pid,
    score: row.score, date: day(row.created), replay: toBase64(bytes) }, text, 200, { 'cache-control': 'max-age=3600' });
}

// ---------------------------------------------------------------------------------------------
// GET /api/rejects, /api/rejects/:id: runs the checks turned away (no player details)
// ---------------------------------------------------------------------------------------------
async function listRejects(env, url, text) {
  const limit = Math.min(300, Math.max(1, parseInt(url.searchParams.get('limit') || '50', 10) || 50));
  const { results } = await env.DB.prepare('SELECT id, created, board, score, version, reason FROM rejects ORDER BY id DESC LIMIT ?').bind(limit).all();
  if (text) return reply({ ok: true, rows: results.map((x) => [x.id, new Date(x.created * 1000).toISOString(), x.board, x.score, x.version, x.reason]) }, true);
  return reply({ ok: true, rejects: results.map((x) => ({ ...x, date: new Date(x.created * 1000).toISOString() })) }, false);
}
async function getReject(env, id, text) {
  const row = await env.DB.prepare('SELECT id, created, board, score, version, reason, data FROM rejects WHERE id = ?').bind(id).first();
  if (!row) return reply({ ok: false, error: 'no such reject' }, text, 404);
  const bytes = unpackReplay(await inflate(fromBase64(row.data)));
  return reply({ ok: true, id: row.id, board: row.board, score: row.score, version: row.version, reason: row.reason, replay: toBase64(bytes) }, text);
}

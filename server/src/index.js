// AirLabs leaderboard: a Cloudflare Worker with a D1 database.
//
//   POST /api/runs              a finished preset run from the plugin (player + replay)
//   POST /api/bests             a player's best score on each board, without replays (runs from
//                               before the leaderboard; sent once by each install)
//   GET  /api/version           the newest AirLabs on bakkesplugins (the plugin's Update button)
//   POST /api/name              change the name shown on the boards
//   GET  /api/boards            every board with its player count and leader
//   GET  /api/boards/:board     one board (?limit=, ?me=<public id>)
//   GET  /api/replays/:run      the replay of a run that is somebody's current best
//   GET  /api/rejects[/:id]     recent runs the checks turned away (reason, board, score, replay; no player)
//   GET  /                      the leaderboard as a web page
//
// Plugin requests add ?format=text and get "key value" lines (rows tab-separated), so the
// plugin needs no JSON parser. Everything else is JSON.
import { boardKey, boardTitle, parseBoard, allBoards, DATA } from './core.js';
import { parseReplay, fromBase64, toBase64, deflate, inflate, packReplay, unpackReplay } from './replay.js';
import { verifyReplay } from './verify.js';
import { PAGE } from './page.js';

const MIN_VERSION = [2, 6, 0];
// The version players can update to: bump it once a release is live on bakkesplugins (installing
// before that would fetch the old one). PLUGIN_ID is AirLabs on bakkesplugins.com/plugin/904.
const LATEST_VERSION = '2.8.1';
const PLUGIN_ID = 904;
const MAX_BESTS = 300;         // boards per /api/bests upload (there are 151)
const UPLOAD_GAP_SEC = 20;     // runs waiting from earlier go up one at a time; real runs take 60 s
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
      if (req.method === 'POST' && p === '/api/bests') return await postBests(req, env, text);
      if (req.method === 'POST' && p === '/api/name') return await postName(req, env, text);
      if (req.method === 'GET' && p === '/api/version') return reply({ ok: true, latest: LATEST_VERSION, plugin: PLUGIN_ID }, text, 200, { 'cache-control': 'max-age=300' });
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
// Players: who's uploading. The first upload hands out a key; later uploads must bring it.
// ---------------------------------------------------------------------------------------------
// Real Rocket League ranks from the plugin: "tier.div,tier.div,tier.div" for 1v1, 2v2, 3v3, with
// "-" for one the game didn't report. Stored as tier * 4 + div (NULL = not known).
export function parseRanks(s) {
  if (typeof s !== 'string') return null;
  const parts = s.split(',');
  if (parts.length !== 3) return null;
  const out = parts.map((x) => {
    const m = /^(\d{1,2})\.(\d)$/.exec(x.trim());
    if (!m) return null;
    const tier = +m[1], div = +m[2];
    return tier <= 22 && div <= 3 ? tier * 4 + div : null;
  });
  return out.some((x) => x !== null) ? out : null;
}

// player.name is the name to show; player.gameName (AirLabs 2.8.2 on) is the one the game reports.
// A name the player picked can't be another player's name (shown or in-game): then the game's
// name is shown instead, and nameTaken says so. opts.upload = false: not an upload (no gap check).
async function authPlayer(req, env, body, t, opts = { upload: true }) {
  const pl = body && body.player;
  if (!pl || typeof pl.id !== 'string' || typeof pl.platform !== 'string') return { error: [400, 'missing player'] };
  const platform = pl.platform.replace(/[^A-Za-z0-9]/g, '').slice(0, 16) || 'Unknown';
  const rawId = pl.id.replace(/[^A-Za-z0-9|_-]/g, '').slice(0, 80);
  if (rawId.length < 3 || /^0+$/.test(rawId)) return { error: [400, 'missing player id'] };
  const id = platform + ':' + rawId;
  const gameName = cleanName(typeof pl.gameName === 'string' && pl.gameName ? pl.gameName : pl.name);
  let name = cleanName(pl.name), nameTaken = false;
  if (name !== gameName) {
    const other = await env.DB.prepare(`SELECT 1 FROM players WHERE id != ?1 AND (lower(name) = lower(?2) OR lower(COALESCE(game_name, '')) = lower(?2)) LIMIT 1`)
      .bind(id, name).first();
    if (other) { name = gameName; nameTaken = true; }
  }

  const player = await env.DB.prepare('SELECT * FROM players WHERE id = ?').bind(id).first();
  let newKey = null;
  if (player) {
    if (player.banned) return { error: [403, 'this account is not allowed on the leaderboard'] };
    if (player.key_hash) {
      if (typeof body.key !== 'string' || (await sha256hex(body.key)) !== player.key_hash)
        return { error: [401, 'this Rocket League account is linked to another AirLabs install'] };
    } else newKey = randomKey();
    if (opts.upload && t - player.last_upload < UPLOAD_GAP_SEC) return { error: [429, 'too many uploads, slow down'] };
  } else {
    const ip = req.headers.get('cf-connecting-ip') || 'local';
    const d = Math.floor(t / 86400);
    const su = await env.DB.prepare('SELECT n FROM signups WHERE ip = ? AND day = ?').bind(ip, d).first();
    if (su && su.n >= SIGNUPS_PER_DAY) return { error: [429, 'too many new players from this network today'] };
    await env.DB.prepare('INSERT INTO signups (ip, day, n) VALUES (?, ?, 1) ON CONFLICT(ip, day) DO UPDATE SET n = n + 1').bind(ip, d).run();
    newKey = randomKey();
  }
  const pid = player ? player.pid : await publicId(id);
  const keyHash = newKey ? await sha256hex(newKey) : player.key_hash;
  if (player) {
    await env.DB.prepare('UPDATE players SET name = ?, game_name = ?, last_upload = ?, key_hash = ? WHERE id = ?')
      .bind(name, gameName, opts.upload ? t : player.last_upload, keyHash, id).run();
  } else {
    await env.DB.prepare('INSERT INTO players (id, pid, name, game_name, platform, key_hash, created, last_upload) VALUES (?, ?, ?, ?, ?, ?, ?, ?)')
      .bind(id, pid, name, gameName, platform, keyHash, t, opts.upload ? t : 0).run();
  }
  // Your own in-game name is yours: anyone showing it as a name they picked goes back to theirs.
  await env.DB.prepare(`UPDATE players SET name = game_name WHERE id != ?1 AND game_name IS NOT NULL
      AND lower(name) = lower(?2) AND lower(game_name) != lower(?2)`).bind(id, gameName).run();
  // Ranks the game reported this time replace the old ones; a playlist it didn't report keeps its old value.
  const ranks = parseRanks(body.ranks);
  if (ranks) {
    await env.DB.prepare(`UPDATE players SET rank1 = COALESCE(?, rank1), rank2 = COALESCE(?, rank2), rank3 = COALESCE(?, rank3),
        ranks_updated = ? WHERE id = ?`).bind(ranks[0], ranks[1], ranks[2], t, id).run();
  }
  return { id, pid, name, platform, newKey, nameTaken };
}

// ---------------------------------------------------------------------------------------------
// POST /api/name  {"player": {"platform", "id", "name", "gameName"}, "key"}: the name to show on the boards
// ---------------------------------------------------------------------------------------------
async function postName(req, env, text) {
  const bad = (status, error) => reply({ ok: false, error }, text, status);
  const bodyText = await req.text();
  if (bodyText.length > 4096) return bad(413, 'too large');
  let body;
  try { body = JSON.parse(bodyText); } catch { return bad(400, 'bad request'); }
  if (!body || !body.player) return bad(400, 'missing player');
  const who = await authPlayer(req, env, body, now(), { upload: false });
  if (who.error) return bad(...who.error);
  return reply({ ok: true, name: who.name, nameTaken: who.nameTaken, pid: who.pid, key: who.newKey,
    message: who.nameTaken ? 'That name belongs to another player, so the boards show your Rocket League name.' : 'Your name on the boards: ' + who.name }, text);
}

// The most a run on this board can score: Track and Chase pay 1000 points a second for 60 s.
const maxScore = (b) => (b.mode === 1 || b.mode === 3 ? 60000 : 500000);

// ---------------------------------------------------------------------------------------------
// POST /api/runs  {"player": {"platform", "id", "name"}, "key": "...", "ranks": "...", "replay": "<base64 ALR1>"}
// ---------------------------------------------------------------------------------------------
async function postRun(req, env, text) {
  const bad = (status, error) => reply({ ok: false, error }, text, status);
  const bodyText = await req.text();
  if (bodyText.length > MAX_BODY) return bad(413, 'upload too large');
  let body;
  try { body = JSON.parse(bodyText); } catch { return bad(400, 'bad request'); }
  if (!body || !body.player) return bad(400, 'missing player');

  let rep, raw;
  try { raw = fromBase64(body.replay || ''); rep = parseReplay(raw); } catch (e) { return bad(400, e.message || 'bad replay'); }
  if (!versionOk(rep.version)) return bad(426, 'update AirLabs to post runs');
  const b = parseBoard(boardKey(rep.mode, rep.preset, rep.range, (rep.flags & 1) !== 0));
  if (!b) return bad(400, 'not a preset run');
  const board = boardKey(b.mode, b.preset, b.range, b.noRoll);
  const t = now();

  const who = await authPlayer(req, env, body, t);
  if (who.error) return bad(...who.error);
  const { id, pid, newKey } = who;
  const shown = { name: who.name, nameTaken: who.nameTaken || undefined };

  // ---- only a new personal best is checked and stored ----
  const prev = await env.DB.prepare('SELECT run, score FROM best WHERE board = ? AND player = ?').bind(board, id).first();
  if (prev && rep.score <= prev.score) {
    await env.DB.prepare('UPDATE best SET plays = plays + 1 WHERE board = ? AND player = ?').bind(board, id).run();
    const pos = await position(env, board, prev.score, null, prev.run, pid);
    return reply({ ok: true, best: false, board, title: boardTitle(b), score: prev.score, pos: pos.pos, of: pos.total, pid, key: newKey, ...shown,
      message: `Not a new best. Your best ${prev.score.toLocaleString('en-US')} is #${pos.pos} of ${pos.total}.` }, text);
  }

  const v = verifyReplay(rep);
  if (!v.ok) { // not a real preset run (wrong ball distance, target size or aim for the preset)
    console.log('rejected', board, rep.score, v.reason);
    // Keep it (without who sent it) so the checks can be looked at against real runs.
    await env.DB.batch([
      env.DB.prepare('INSERT INTO rejects (created, board, score, version, reason, data) VALUES (?, ?, ?, ?, ?, ?)')
        .bind(t, board, rep.score, rep.version, v.reason, toBase64(await deflate(packReplay(raw)))),
      env.DB.prepare('DELETE FROM rejects WHERE id <= (SELECT MAX(id) - 300 FROM rejects)'),
    ]);
    return reply({ ok: false, error: 'run not accepted: ' + v.reason, pid, key: newKey }, text, 422);
  }

  // The run goes up with the score it claims. What the replay check noticed (and the score it
  // replayed) is kept with the run for reference only; nothing is shown or turned away for it.
  const score = rep.score;
  const flags = v.flags.concat(v.score !== null && v.score !== rep.score ? [`replayed:${v.score}`] : []);
  const stored = toBase64(await deflate(packReplay(raw)));
  const ins = await env.DB.prepare(
    'INSERT INTO runs (player, board, score, created, version, targets, misses, on_pct, avg_time, flags) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?) RETURNING id')
    .bind(id, board, score, t, rep.version, v.stats.targets, v.stats.misses, v.stats.onPct, v.stats.avgTime, flags.join(' ')).first();
  const runId = ins.id;
  const ops = [
    env.DB.prepare('INSERT INTO replays (run, data) VALUES (?, ?)').bind(runId, stored),
    env.DB.prepare(`INSERT INTO best (board, player, run, score, created) VALUES (?, ?, ?, ?, ?)
      ON CONFLICT(board, player) DO UPDATE SET run = excluded.run, score = excluded.score, created = excluded.created, plays = plays + 1`)
      .bind(board, id, runId, score, t),
  ];
  if (prev) ops.push(env.DB.prepare('DELETE FROM replays WHERE run = ?').bind(prev.run)); // only the best keeps its replay
  await env.DB.batch(ops);
  const pos = await position(env, board, score, t, runId, pid);
  return reply({
    ok: true, best: true, board, title: boardTitle(b), score, pos: pos.pos, of: pos.total, run: runId, pid, key: newKey, ...shown,
    message: `New best posted: #${pos.pos} of ${pos.total}.`,
  }, text);
}

// ---------------------------------------------------------------------------------------------
// POST /api/bests  {"player", "key", "ranks", "version": "2.8.0",
//                   "bests": [{"board": "0-4-1-0", "score": 12345, "targets": 20, "misses": 1, "onPct": 80.5, "date": <unix s>,
//                              "input": "controller" | "keyboard"}]}
// A player's best score on each board from their own saved runs, including runs from before the
// leaderboard (so without replays). Each one that beats what the board has for them goes up.
// ---------------------------------------------------------------------------------------------
async function postBests(req, env, text) {
  const bad = (status, error) => reply({ ok: false, error }, text, status);
  const bodyText = await req.text();
  if (bodyText.length > MAX_BODY) return bad(413, 'upload too large');
  let body;
  try { body = JSON.parse(bodyText); } catch { return bad(400, 'bad request'); }
  if (!body || !body.player) return bad(400, 'missing player');
  if (!versionOk(body.version)) return bad(426, 'update AirLabs to post runs');
  if (!Array.isArray(body.bests) || body.bests.length > MAX_BESTS) return bad(400, 'bad list of bests');
  const t = now();
  const who = await authPlayer(req, env, body, t);
  if (who.error) return bad(...who.error);
  const { id, pid, newKey } = who;
  const version = String(body.version).replace(/[^0-9.]/g, '').slice(0, 16);

  // One per board (the highest), only real preset boards, only possible scores.
  const wanted = new Map();
  for (const e of body.bests) {
    const b = e && parseBoard(String(e.board));
    const score = e && Number.isInteger(e.score) ? e.score : -1;
    if (!b || score < 0 || score > maxScore(b)) continue;
    const key = boardKey(b.mode, b.preset, b.range, b.noRoll);
    if (!wanted.has(key) || wanted.get(key).score < score) wanted.set(key, { ...e, score, board: key });
  }
  let posted = 0;
  for (const e of wanted.values()) {
    const prev = await env.DB.prepare('SELECT run, score FROM best WHERE board = ? AND player = ?').bind(e.board, id).first();
    if (prev && e.score === prev.score && (e.input === 'keyboard' || e.input === 'controller')) {
      // The same saved best sent again, now saying which input it was: fill that in.
      await env.DB.prepare(`UPDATE runs SET flags = flags || ' ' || ? WHERE id = ? AND flags LIKE '%saved-best%'
          AND flags NOT LIKE '%keyboard%' AND flags NOT LIKE '%controller%'`).bind(e.input, prev.run).run();
    }
    if (prev && e.score <= prev.score) continue;
    const when = Number.isInteger(e.date) && e.date > 1500000000 && e.date <= t ? e.date : t;
    const int = (x) => (Number.isInteger(x) && x >= 0 && x < 100000 ? x : 0);
    const pct = typeof e.onPct === 'number' && e.onPct >= 0 && e.onPct <= 100 ? e.onPct : null;
    const ins = await env.DB.prepare(
      'INSERT INTO runs (player, board, score, created, version, targets, misses, on_pct, avg_time, flags) VALUES (?, ?, ?, ?, ?, ?, ?, ?, NULL, ?) RETURNING id')
      .bind(id, e.board, e.score, when, version, int(e.targets), int(e.misses), pct,
        'noreplay saved-best' + (e.input === 'keyboard' ? ' keyboard' : e.input === 'controller' ? ' controller' : '')).first();
    const ops = [env.DB.prepare(`INSERT INTO best (board, player, run, score, created) VALUES (?, ?, ?, ?, ?)
        ON CONFLICT(board, player) DO UPDATE SET run = excluded.run, score = excluded.score, created = excluded.created`)
      .bind(e.board, id, ins.id, e.score, when)];
    if (prev) ops.push(env.DB.prepare('DELETE FROM replays WHERE run = ?').bind(prev.run));
    await env.DB.batch(ops);
    ++posted;
  }
  return reply({ ok: true, posted, of: wanted.size, pid, key: newKey, name: who.name, nameTaken: who.nameTaken || undefined,
    message: posted ? `${posted} of your saved bests went up.` : 'Your saved bests were already up.' }, text);
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
  const { results } = await env.DB.prepare(`SELECT b.score, b.created, b.run, p.pid, p.name, p.platform, p.rank1, p.rank2, p.rank3,
        r.targets, r.misses, r.on_pct, r.avg_time, r.flags, (SELECT COUNT(*) FROM replays rp WHERE rp.run = b.run) AS has_replay
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
  const rank = (v) => (v === null || v === undefined ? null : { tier: v >> 2, div: v & 3 });
  // Controller or keyboard: runs with replays carry it (the replay check notes "keyboard"); saved
  // bests say which from AirLabs 2.8.2 on, and older ones aren't known.
  const input = (flags) => {
    const f = ' ' + (flags || '') + ' ';
    if (f.includes(' keyboard ')) return 'keyboard';
    if (f.includes(' controller ') || !f.includes(' saved-best ')) return 'controller';
    return null;
  };
  const entries = results.map((x, i) => ({
    pos: i + 1, pid: x.pid, name: x.name, platform: x.platform, score: x.score,
    date: day(x.created), run: x.run, targets: x.targets, misses: x.misses, onPct: x.on_pct, avgTime: x.avg_time,
    replay: x.has_replay > 0, ranks: [rank(x.rank1), rank(x.rank2), rank(x.rank3)], input: input(x.flags),
  }));
  const max = maxScore(b) <= 60000 ? maxScore(b) : null; // only where there is a real ceiling
  if (text) {
    // Old plugins read the first seven fields; replay (1/0), the 1v1/2v2/3v3 ranks ("tier.div" or "-")
    // and the input (c = controller, k = keyboard, - = not known) follow.
    const r = (k) => (k ? k.tier + '.' + k.div : '-');
    return reply({ ok: true, board: key, title: boardTitle(b), total, max, me: mine,
      rows: entries.map((e) => [e.pos, e.pid, cleanText(e.name), e.platform, e.score, e.date, e.run, e.replay ? 1 : 0, r(e.ranks[0]), r(e.ranks[1]), r(e.ranks[2]), e.input === 'keyboard' ? 'k' : e.input ? 'c' : '-']) },
      true, 200, { 'cache-control': 'max-age=10' });
  }
  return reply({ ok: true, board: key, title: boardTitle(b), total, max, me: mine, entries }, false, 200, { 'cache-control': 'max-age=10' });
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

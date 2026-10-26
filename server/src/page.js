// The leaderboard as a web page (served at /). It reads the same API as the plugin.
export const PAGE = `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>AirLabs Leaderboard</title>
<meta name="description" content="Public leaderboards for AirLabs, the aerial orientation trainer for Rocket League.">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&display=swap" rel="stylesheet">
<style>
  :root {
    color-scheme: dark;
    --bg: #0e1117; --side: #0a0c11; --card: #161b24; --card-hi: #1d232f; --border: #252c3a;
    --text: #e6eaf1; --dim: #8e97a8; --faint: #5d6677; --accent: #3b82f6; --accent-hi: #5694f8; --gold: #f5c451;
  }
  * { box-sizing: border-box; }
  body { margin: 0; background: var(--bg); color: var(--text); font: 15px/1.45 Inter, system-ui, sans-serif; -webkit-font-smoothing: antialiased; }
  .wrap { max-width: 920px; margin: 0 auto; padding: 28px 16px 64px; }
  header { display: flex; align-items: center; gap: 14px; margin-bottom: 6px; }
  .logo { width: 34px; height: 34px; flex: none; }
  h1 { font-size: 24px; font-weight: 700; margin: 0; letter-spacing: -0.01em; }
  .sub { color: var(--dim); margin: 0 0 22px 48px; }
  .bar { display: flex; flex-wrap: wrap; gap: 10px; align-items: center; margin-bottom: 12px; }
  .seg { display: inline-flex; background: var(--card); border: 1px solid var(--border); border-radius: 10px; padding: 3px; }
  .seg button, .chip { font: inherit; color: var(--dim); background: none; border: 0; cursor: pointer; }
  .seg button { padding: 6px 14px; border-radius: 7px; font-weight: 600; }
  .seg button[aria-pressed="true"] { background: var(--card-hi); color: var(--text); box-shadow: inset 0 -2px 0 var(--accent); }
  .chips { display: flex; flex-wrap: wrap; gap: 8px; margin-bottom: 18px; }
  .chip { padding: 6px 14px; border-radius: 999px; border: 1px solid var(--border); background: var(--card); font-weight: 500; }
  .chip:hover, .seg button:hover { color: var(--text); }
  .chip[aria-pressed="true"] { border-color: var(--accent); color: var(--text); background: rgba(59,130,246,.18); }
  .chip small { color: var(--faint); margin-left: 6px; font-weight: 500; }
  .toggle { display: inline-flex; align-items: center; gap: 8px; color: var(--dim); cursor: pointer; user-select: none; }
  .toggle input { accent-color: var(--accent); width: 16px; height: 16px; }
  .card { background: var(--card); border: 1px solid var(--border); border-radius: 12px; overflow: hidden; }
  .card-head { display: flex; justify-content: space-between; align-items: baseline; gap: 12px; padding: 14px 18px; border-bottom: 1px solid var(--border); }
  .card-head h2 { margin: 0; font-size: 17px; font-weight: 600; }
  .card-head span { color: var(--faint); font-size: 13px; }
  table { width: 100%; border-collapse: collapse; }
  th { text-align: left; font-size: 12px; font-weight: 600; text-transform: uppercase; letter-spacing: .05em; color: var(--faint); padding: 10px 18px; }
  td { padding: 11px 18px; border-top: 1px solid var(--border); }
  tr:first-child td { border-top: 0; }
  td.pos { width: 54px; color: var(--dim); font-variant-numeric: tabular-nums; font-weight: 600; }
  tr.top1 td.pos { color: var(--gold); }
  td.score { text-align: right; font-weight: 700; font-variant-numeric: tabular-nums; }
  th.score { text-align: right; }
  .name { font-weight: 600; }
  .plat { color: var(--faint); font-size: 12px; margin-left: 8px; }
  .tier { display: inline-block; min-width: 40px; text-align: center; font-size: 12px; font-weight: 700; padding: 2px 7px; border-radius: 5px; color: #fff; }
  .detail, .date { color: var(--dim); font-size: 13px; font-variant-numeric: tabular-nums; }
  .empty { padding: 36px 18px; text-align: center; color: var(--dim); }
  footer { margin-top: 22px; color: var(--faint); font-size: 13px; }
  footer a { color: var(--accent-hi); text-decoration: none; }
  @media (max-width: 640px) { .hide-sm { display: none; } td, th { padding-left: 12px; padding-right: 12px; } .sub { margin-left: 0; } }
</style>
</head>
<body>
<div class="wrap">
  <header>
    <svg class="logo" viewBox="0 0 34 34" aria-hidden="true"><circle cx="17" cy="17" r="13" fill="none" stroke="#3b82f6" stroke-width="2.5"/><circle cx="17" cy="17" r="6" fill="none" stroke="#5694f8" stroke-width="2"/><circle cx="17" cy="17" r="2.2" fill="#fff"/></svg>
    <h1>AirLabs Leaderboard</h1>
  </header>
  <p class="sub">Everyone's best one-minute run on every AirLabs preset. Every run is checked by replaying it, and you can watch any of them in the game.</p>

  <div class="bar">
    <div class="seg" id="modes" role="group" aria-label="Mode"></div>
    <div class="seg" id="ranges" role="group" aria-label="Range"></div>
    <label class="toggle" id="rollWrap"><input type="checkbox" id="noroll"> No air roll</label>
  </div>
  <div class="chips" id="presets" role="group" aria-label="Preset"></div>

  <div class="card">
    <div class="card-head"><h2 id="title">Loading…</h2><span id="count"></span></div>
    <div id="board"><div class="empty">Loading…</div></div>
  </div>
  <footer>
    To post your runs, turn on <b>Post my runs</b> on the Leaderboard page in AirLabs. To watch a run, press <b>Watch</b> next to it there.
    · <a href="https://github.com/triadic-zeno/airlabs-orientation-trainer">AirLabs on GitHub</a>
  </footer>
</div>
<script>
const RANK_COLORS = ['#b07a4a', '#a9b4c2', '#e8b931', '#3fc1c9', '#4f8df5', '#9b59f5', '#e5484d', '#f0e8cd', '#ffaa28'];
const DARK_TEXT = new Set([0, 1, 2, 3, 7, 8]);
const rankOf = (t) => t < 21 ? Math.floor(t / 3) : t === 21 ? 7 : 8;
let meta = null;
const sel = { mode: 0, preset: 4, range: 1, noRoll: false };

const el = (tag, attrs = {}, ...kids) => {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) if (k === 'class') e.className = v; else e.setAttribute(k, v);
  for (const k of kids) e.append(k);
  return e;
};
const usesRange = (m, p) => !(m === 0 && meta.presets[0][p] === 'Flip Reset');
const rollAllowed = (m, p) => !(m === 0 && meta.presets[0][p] === 'Flip Reset');
const key = () => [sel.mode, sel.preset, usesRange(sel.mode, sel.preset) ? sel.range : 0, sel.noRoll && rollAllowed(sel.mode, sel.preset) ? 1 : 0].join('-');

function seg(id, labels, current, onPick) {
  const box = document.getElementById(id);
  box.replaceChildren(...labels.map((l, i) => {
    const b = el('button', { type: 'button', 'aria-pressed': String(i === current) }, l);
    b.onclick = () => onPick(i);
    return b;
  }));
}
function render() {
  seg('modes', meta.modes, sel.mode, (i) => { sel.mode = i; sel.preset = i === 0 ? 4 : 1; go(); });
  const ur = usesRange(sel.mode, sel.preset);
  document.getElementById('ranges').style.display = ur ? '' : 'none';
  seg('ranges', meta.ranges, sel.range, (i) => { sel.range = i; go(); });
  document.getElementById('rollWrap').style.display = rollAllowed(sel.mode, sel.preset) ? '' : 'none';
  document.getElementById('noroll').checked = sel.noRoll;
  const counts = Object.fromEntries(meta.boards.map((b) => [b.board, b.players]));
  document.getElementById('presets').replaceChildren(...meta.presets[sel.mode].map((name, i) => {
    const k = [sel.mode, i, usesRange(sel.mode, i) ? sel.range : 0, sel.noRoll && rollAllowed(sel.mode, i) ? 1 : 0].join('-');
    const b = el('button', { type: 'button', class: 'chip', 'aria-pressed': String(i === sel.preset) }, name);
    if (counts[k]) b.append(el('small', {}, String(counts[k])));
    b.onclick = () => { sel.preset = i; go(); };
    return b;
  }));
}
async function go(push = true) {
  render();
  const k = key();
  if (push) history.replaceState(null, '', '#' + k);
  const board = document.getElementById('board');
  board.replaceChildren(el('div', { class: 'empty' }, 'Loading…'));
  try {
    const r = await (await fetch('/api/boards/' + k + '?limit=100')).json();
    if (k !== key()) return;
    document.getElementById('title').textContent = r.title;
    document.getElementById('count').textContent = r.total ? r.total + (r.total === 1 ? ' player' : ' players') : '';
    if (!r.entries || !r.entries.length) {
      board.replaceChildren(el('div', { class: 'empty' }, 'No runs on this board yet. Play it in AirLabs with Post my runs on, and the top spot is yours.'));
      return;
    }
    const tracky = sel.mode === 1 || sel.mode === 3;
    const head = el('tr', {}, el('th', {}, '#'), el('th', {}, 'Player'), el('th', {}, 'Rank'),
      el('th', { class: 'hide-sm' }, tracky ? 'On the ball' : 'Targets'), el('th', { class: 'hide-sm' }, 'Date'), el('th', { class: 'score' }, 'Score'));
    const rows = r.entries.map((e) => {
      const rk = rankOf(e.tier);
      const pill = el('span', { class: 'tier', style: 'background:' + RANK_COLORS[rk] + ';color:' + (DARK_TEXT.has(rk) ? '#14161c' : '#fff') }, meta.tiers[e.tier]);
      const detail = tracky ? (e.onPct != null ? e.onPct.toFixed(1) + '%' : '') : (e.targets + ' hit' + (e.misses ? ', ' + e.misses + ' missed' : ''));
      return el('tr', { class: e.pos === 1 ? 'top1' : '' },
        el('td', { class: 'pos' }, String(e.pos)),
        el('td', {}, el('span', { class: 'name' }, e.name), el('span', { class: 'plat' }, e.platform)),
        el('td', {}, pill),
        el('td', { class: 'detail hide-sm' }, detail),
        el('td', { class: 'date hide-sm' }, e.date),
        el('td', { class: 'score' }, e.score.toLocaleString('en-US')));
    });
    board.replaceChildren(el('table', {}, el('thead', {}, head), el('tbody', {}, ...rows)));
  } catch (e) {
    board.replaceChildren(el('div', { class: 'empty' }, "Couldn't load this board. Try again in a moment."));
  }
}
document.getElementById('noroll').onchange = (e) => { sel.noRoll = e.target.checked; go(); };
(async () => {
  meta = await (await fetch('/api/boards')).json();
  const h = /^#(\\d)-(\\d+)-(\\d)-([01])$/.exec(location.hash);
  if (h) Object.assign(sel, { mode: +h[1], preset: +h[2], range: +h[3], noRoll: h[4] === '1' });
  go(false);
})();
</script>
</body>
</html>`;

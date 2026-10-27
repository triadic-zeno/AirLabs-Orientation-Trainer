// Scores a replay again from its samples, and notes anything unusual about it.
//
// The replay holds, for every tick, the car's rotation and the direction to the ball. That's all
// a run's score depends on, so the server replays the plugin's own rules over it (when a target
// counts as reached, the 0.2 s hold, the 5 s limit, points per flick, tracking credit per tick)
// and gets the score itself.
//
// None of this turns a run away or is shown anywhere: the replayed score and anything unusual
// (timing, spin speed, roll on No air roll, ball speed, turn sizes, events that don't line up, a
// score past the Pro mark) are only kept with the run, for reference. The only runs refused are
// ones that aren't a preset at all (wrong ball distance, target size or aim).
// (Real game data showed why: the car's rotation can update every other tick, which made honest
// runs look like they spun at twice the game's limit.)
import {
  C, DT, FLICK, TRACK, HYBRID, CHASE, flickPoints, trackCredit, ballTargetDeg,
  presetCount, presetList, presetDistance, presetAim, clamp,
} from './core.js';
import { EV, RF } from './replay.js';

const F = Math.fround;
const HOLD = F(C.presetHoldSec), TIMEOUT = F(C.presetTimeout), DELAY = F(C.presetDelaySec);
const HYB_TRACK = F(C.hybridTrackSec), CHASE_HOLD = F(C.chaseHoldSec), CHASE_TIMEOUT = F(C.chaseTimeout);
const RUN_SEC = F(C.presetRunSec);
const DEG = Math.PI / 180;


// sin of every rotator value (65536 per turn), built once per isolate: the car's axes each tick
// are then plain arithmetic.
let SIN = null;
function sinTable() {
  if (!SIN) {
    SIN = new Float64Array(65536);
    for (let i = 0; i < 65536; i++) SIN[i] = Math.sin(i * Math.PI / 32768);
  }
  return SIN;
}

class Reject extends Error {}
const reject = (msg) => { throw new Reject(msg); };
class Broken extends Error {}
const broken = (msg) => { throw new Broken(msg); }; // the events can't be followed any further

export function verifyReplay(r) {
  try {
    return verify(r);
  } catch (e) {
    if (e instanceof Reject) return { ok: false, reason: e.message };
    throw e;
  }
}

function verify(r) {
  const notes = new Set();
  const note = (code) => notes.add(code);
  // ---- the preset ----
  if (r.mode > 3 || r.preset >= presetCount(r.mode)) reject('not a preset run');
  if (r.range > 2) reject('bad range');
  const preset = presetList(r.mode)[r.preset];
  const aim = presetAim(r.mode, r.preset);
  if (r.aim !== aim) reject("aim doesn't match the preset");
  const noRoll = (r.flags & RF.NOROLL) !== 0;
  if (noRoll && aim !== 0) reject('No air roll does not apply to wheels aim');
  const distance = presetDistance(r.mode, r.preset, r.range);
  if (Math.abs(r.distance - distance) > 0.01) reject("ball distance doesn't match the preset");
  if (Math.abs(r.targetDeg - ballTargetDeg(distance)) > 0.01) reject("target size doesn't match the preset");
  const target = F(r.targetDeg);

  // ---- timing ----
  const n = r.n;
  const tickTiming = (r.flags & RF.TICKTIMING) !== 0;
  const t = new Float64Array(n), dt = new Float64Array(n);
  if (tickTiming) {
    if (Math.abs(n - 7200) > 2) note('ticks');
    let acc = 0;
    for (let i = 0; i < n; i++) {
      acc += DT; t[i] = acc; dt[i] = DT;
      if (Math.abs(r.ms[i] - t[i] * 1000) > 2) note('timing');
    }
  } else {
    note('clock');
    // Timed by the PC clock: the hook fired at some other rate (40 to 300 a second is accepted).
    if (n < 2400 || n > 18000) note('ticks');
    let prev = 0;
    for (let i = 0; i < n; i++) {
      t[i] = r.ms[i] / 1000;
      dt[i] = t[i] - prev;
      if (dt[i] < 0 || dt[i] > 0.06) { note('timing'); dt[i] = clamp(dt[i], 0, 0.06); } // the plugin caps a tick at 50 ms; ms rounding adds 1

      prev = t[i];
    }
  }
  const slack = tickTiming ? 1e-6 : 0.0015; // sample times are whole milliseconds
  if (t[n - 1] < RUN_SEC - slack || (n > 1 && t[n - 2] >= RUN_SEC + slack)) note('length');
  if (r.runSec < 59.99 || r.runSec > 60.2) note('length');
  const tol = tickTiming ? 1 : 3; // ticks of slack for an event

  // ---- the car, tick by tick: its axes (BasisFromRotator, from the sine table), its aim, and
  // the physics checks: how far it turned since the last tick (no car spins faster than 5.5 rad/s)
  // and how much of that was roll (none on No air roll runs). The angle turned in a tick is
  // acos((trace - 1) / 2) of the rotation between the ticks, compared as a cosine. ----
  const S = sinTable();
  const A = new Float64Array(n * 3);
  let overSpin = 0, maxSpin = 0, rollSum = 0, minCos = 1;
  const cosLimit = Math.cos((C.maxSpin * 1.05 + 0.05) * DT);
  let f0 = 0, f1 = 0, f2 = 0, r0 = 0, r1 = 0, r2 = 0, u0 = 0, u1 = 0, u2 = 0;
  for (let i = 0; i < n; i++) {
    const p = r.rot[i * 3] & 0xFFFF, y = r.rot[i * 3 + 1] & 0xFFFF, rl = r.rot[i * 3 + 2] & 0xFFFF;
    const sp = S[p], cp = S[(p + 16384) & 0xFFFF], sy = S[y], cy = S[(y + 16384) & 0xFFFF], sr = S[rl], cr = S[(rl + 16384) & 0xFFFF];
    const F0 = cp * cy, F1 = cp * sy, F2 = sp;
    const R0 = sr * sp * cy - cr * sy, R1 = sr * sp * sy + cr * cy, R2 = -sr * cp;
    const U0 = -(cr * sp * cy + sr * sy), U1 = cy * sr - cr * sp * sy, U2 = cr * cp;
    if (aim === 1) { A[i * 3] = -U0; A[i * 3 + 1] = -U1; A[i * 3 + 2] = -U2; } else { A[i * 3] = F0; A[i * 3 + 1] = F1; A[i * 3 + 2] = F2; }
    if (i > 0) {
      const c = (F0 * f0 + F1 * f1 + F2 * f2 + R0 * r0 + R1 * r1 + R2 * r2 + U0 * u0 + U1 * u1 + U2 * u2 - 1) / 2;
      const step = tickTiming ? DT : Math.max(dt[i], 1e-3);
      if (tickTiming) {
        if (c < minCos) minCos = c;
        if (c < cosLimit) overSpin++;
      } else maxSpin = Math.max(maxSpin, Math.acos(clamp(c, -1, 1)) / step);
      // roll: how far the right axis tipped toward the roof (and the roof toward the right); sin x ~ x
      rollSum += Math.abs((R0 * u0 + R1 * u1 + R2 * u2 - (U0 * r0 + U1 * r1 + U2 * r2)) / 2) / step;
    }
    f0 = F0; f1 = F1; f2 = F2; r0 = R0; r1 = R1; r2 = R2; u0 = U0; u1 = U1; u2 = U2;
  }
  if (tickTiming) maxSpin = Math.acos(clamp(minCos, -1, 1)) / DT;
  const D = r.dir;
  const cosT = Math.cos(target * DEG), cos2T = Math.cos(2 * target * DEG);
  // Cosine of the angle between the aim on tick i and the ball direction stored on tick j.
  const cosErr = (i, j) => A[i * 3] * D[j * 3] + A[i * 3 + 1] * D[j * 3 + 1] + A[i * 3 + 2] * D[j * 3 + 2];
  const err = (i, j) => Math.acos(clamp(cosErr(i, j), -1, 1)) / DEG;
  const cosDir = (a, b) => D[a * 3] * D[b * 3] + D[a * 3 + 1] * D[b * 3 + 1] + D[a * 3 + 2] * D[b * 3 + 2];
  const dirAngle = (a, b) => Math.acos(clamp(cosDir(a, b), -1, 1)) / DEG;
  const elevation = (j) => Math.asin(clamp(D[j * 3 + 2], -1, 1)) / DEG;

  if (tickTiming && (maxSpin > C.maxSpin * 4 || overSpin > Math.max(12, n * 0.01))) note('spin');
  const meanRoll = rollSum / Math.max(1, n - 1);
  if (noRoll && meanRoll > 0.25) note('roll');

  // ---- the run itself ----
  const evs = r.events.slice().sort((a, b) => a.tick - b.tick);
  const out = { score: 0, targets: 0, misses: 0, hybReps: 0, onTime: 0, trackTime: 0, sumTime: 0, offTurn: 0 };
  const nextSpawnTick = (after) => { let j = after + 1; while (j < n && t[j] < t[after] + DELAY) j++; return j; };
  const track = (j) => {
    const c = cosErr(j, j);
    out.trackTime += dt[j];
    if (c >= cosT) { out.score += 1000 * dt[j]; out.onTime += dt[j]; }
    else if (c > cos2T) out.score += 1000 * trackCredit(Math.acos(clamp(c, -1, 1)) / DEG, target) * dt[j];
  };

  // One Flick / Hybrid target, spawned on tick s: walk the plugin's rules forward.
  const flickTarget = (s) => {
    const ts = t[s];
    let reached = false, firstReach = 0, locking = false, lockStart = 0;
    for (let i = s; i < n; i++) {
      const inCone = cosErr(i, s) >= cosT;
      if (inCone && !reached) { reached = true; firstReach = t[i]; }
      if (inCone) {
        if (!locking) { locking = true; lockStart = t[i]; }
        if (t[i] - lockStart >= HOLD) return { type: EV.HIT, tick: i, time: (reached ? firstReach : lockStart) - ts };
      } else locking = false;
      if (t[i] - ts > TIMEOUT) return { type: EV.MISS, tick: i };
    }
    return { type: 0, tick: n };
  };
  const cosStill = Math.cos(0.05 * DEG);
  const checkBallStill = (from, to, what) => {
    for (let j = from + 1; j <= Math.min(to, n - 1); j++)
      if (cosDir(from, j) < cosStill) { note('ball'); return; }
  };
  // Ball speed over a stretch of ticks [a, b]: angle covered between a-1 and b, per second.
  const speedCheck = (a, b, lo, hi, what) => {
    const W = 12;
    for (let j = Math.max(a, 1); j + W <= b; j += W) {
      const v = dirAngle(j - 1, j - 1 + W) / (t[j - 1 + W] - t[j - 1]);
      if (v > hi * 1.02 + 0.5 || v < lo * 0.95 - 0.5) { note('ball-speed'); return; }
    }
  };

  let scored = true;
  try {
  if (r.mode === FLICK || r.mode === HYBRID) {
    const minTurn = r.mode === FLICK ? preset.minTurn : C.hybridFlickMin;
    const maxTurn = r.mode === FLICK ? preset.maxTurn : C.hybridFlickMax;
    const bandMin = r.mode === FLICK ? preset.bandMin : -90, bandMax = r.mode === FLICK ? preset.bandMax : 90;
    const lo = preset.dynamic ? preset.vMin : preset.speed, hi = preset.dynamic ? preset.vMax : preset.speed;
    let expect = 0, k = 0;
    while (true) {
      const e = evs[k];
      if (!e) {
        if (expect < n) note('events');
        break;
      }
      if (e.type !== EV.SPAWN) broken('expected a new target');
      if (Math.abs(e.tick - expect) > tol) note('timing');
      const s = e.tick;
      if (s >= n) break;
      if (s > 0 && dirAngle(s - 1, s) < 0.05) note('ball');
      const turn = err(s, s);
      if (Math.abs(turn - e.value) > 0.2) note('turns');
      if (turn < minTurn - 0.3 || turn > maxTurn + 0.3) out.offTurn++;
      const el = elevation(s);
      if (el < bandMin - 0.2 || el > bandMax + 0.2) note('ball');

      const sim = flickTarget(s);
      const o = evs[k + 1];
      if (!o || o.tick >= n) {
        if (sim.type && sim.tick < n - tol) note('events');
        break;
      }
      if (o.type !== EV.HIT && o.type !== EV.MISS) broken('expected a hit or a miss');
      if (o.type !== sim.type || Math.abs(o.tick - sim.tick) > tol) note('events');
      checkBallStill(s, o.tick, 'during a flick');
      // Scored by what the samples show (sim), counted by what the plugin said (o).
      if (sim.type === EV.HIT) {
        const pts = flickPoints(sim.time, turn);
        if (o.type === EV.HIT && Math.abs(pts - o.value) > Math.max(40, 0.04 * pts)) note('points');
        out.score += pts;
        out.sumTime += sim.time;
      } else if (sim.type === EV.MISS) out.score = Math.max(0, out.score - C.missPenalty);
      if (o.type === EV.HIT) out.targets++; else out.misses++;
      k += 2;
      let last = o.tick;
      if (r.mode === HYBRID && o.type === EV.HIT) {
        const hs = evs[k], he = evs[k + 1];
        if (!hs || hs.type !== EV.HYB_START || hs.tick !== o.tick) broken('expected the Hybrid ball to take off');
        if (!he || he.type !== EV.HYB_END) broken('expected the Hybrid tracking to end');
        let endSim = o.tick + 1;
        while (endSim < n && t[endSim] < t[o.tick] + HYB_TRACK) endSim++;
        if (endSim < n ? Math.abs(he.tick - endSim) > tol : he.tick < n - 1) note('timing');
        const end = Math.min(he.tick, n - 1);
        for (let j = o.tick + 1; j <= end; j++) track(j);
        speedCheck(o.tick + 1, end, lo, hi, 'while tracking');
        out.hybReps++;
        k += 2;
        last = he.tick;
        if (he.tick >= n) break;
      }
      expect = nextSpawnTick(last);
    }
    if (out.offTurn > 2) note('turns');
  } else if (r.mode === TRACK) {
    if (evs.length) note('events');
    for (let j = 0; j < n; j++) track(j);
    const lo = preset.dynamic ? preset.vMin : preset.speed, hi = preset.dynamic ? preset.vMax : preset.speed;
    speedCheck(0, n - 1, lo, hi, '');
  } else if (r.mode === CHASE) {
    for (let j = 0; j < n; j++) track(j);
    const lo = preset.dynamic ? preset.vMin : preset.speed, hi = preset.dynamic ? preset.vMax : preset.speed;
    if (!evs.length || evs[0].type !== EV.CH_LAUNCH || evs[0].tick !== 0) broken('a Chase run starts with the ball dashing off');
    // Where the aim is on the ball, and since when (reset when a catch sends the ball off).
    const catchTicks = new Set(evs.filter((e) => e.type === EV.CH_CATCH).map((e) => e.tick));
    const onSince = new Float64Array(n);
    let since = -1;
    for (let j = 0; j < n; j++) {
      if (cosErr(j, j) >= cosT) { if (since < 0) since = t[j]; } else since = -1;
      onSince[j] = since;
      if (catchTicks.has(j)) since = -1;
    }
    let launch = -1, k = 1; // launch tick (-1 = the start of the run)
    while (true) {
      const stop = evs[k];
      if (!stop || stop.tick >= n) break;
      if (stop.type !== EV.CH_STOP) broken('expected the ball to stop');
      const tl = launch < 0 ? 0 : t[launch];
      const dur = t[stop.tick] - tl;
      if (dur < C.chaseMoveMin - 1e-6 || dur > C.chaseMoveMax + DT + 1e-6) note('timing');
      const a = launch < 0 ? 0 : launch, steps = stop.tick - a;
      const v = dirAngle(a, stop.tick) / Math.max(1e-3, t[stop.tick] - t[a]);
      if (v > hi * 1.01 + 0.3 || v < lo * 0.99 - 0.3) note('ball-speed');
      const mid = a + (steps >> 1);
      if (Math.abs(dirAngle(a, mid) + dirAngle(mid, stop.tick) - dirAngle(a, stop.tick)) > 0.5) note('ball');
      // stopped: the plugin checks for a catch (or a miss) every tick from the stop on
      const ts = t[stop.tick];
      let simType = 0, simTick = n;
      for (let j = stop.tick; j < n; j++) {
        if (onSince[j] >= 0 && t[j] - Math.max(onSince[j], ts) >= CHASE_HOLD) { simType = EV.CH_CATCH; simTick = j; break; }
        if (t[j] - ts > CHASE_TIMEOUT) { simType = EV.CH_MISS; simTick = j; break; }
      }
      const o = evs[k + 1];
      if (!o || o.tick >= n) {
        if (simType && simTick < n - tol) note('events');
        checkBallStill(stop.tick, n - 1, 'while stopped');
        break;
      }
      if (o.type !== EV.CH_CATCH && o.type !== EV.CH_MISS) broken('expected a catch or a miss');
      if (o.type !== simType || Math.abs(o.tick - simTick) > tol) note('events');
      checkBallStill(stop.tick, o.tick, 'while stopped');
      const l = evs[k + 2];
      if (!l || l.type !== EV.CH_LAUNCH || l.tick !== o.tick) broken('expected the ball to dash off again');
      if (o.type === EV.CH_CATCH) { out.targets++; out.sumTime += t[o.tick] - ts; } else out.misses++;
      launch = o.tick;
      k += 3;
    }
  }

  } catch (e) {
    if (!(e instanceof Broken)) throw e;
    note('events');
    scored = false; // the events couldn't be followed to the end: the claimed score stands
  }

  // ---- totals ----
  if (scored && (out.targets !== r.targets || out.misses !== r.misses || out.hybReps !== r.hybReps)) note('counts');
  const score = scored ? Math.round(out.score) : null;
  // The board shows the replayed score when it's clearly different from the claimed one.
  let boardScore = r.score;
  if (scored && Math.abs(score - r.score) > Math.max(25, 0.005 * score)) { note('score'); boardScore = score; }
  if (r.flags & RF.KEYBOARD) note('keyboard');
  return {
    ok: true,
    score,
    boardScore,
    flags: [...notes],
    stats: {
      targets: out.targets, misses: out.misses, hybReps: out.hybReps,
      onPct: out.trackTime > 0 ? Math.round(1000 * out.onTime / out.trackTime) / 10 : null,
      avgTime: out.targets && r.mode !== TRACK ? Math.round(1000 * out.sumTime / out.targets) / 1000 : null,
      maxSpin: Math.round(maxSpin * 100) / 100, meanRoll: Math.round(meanRoll * 1000) / 1000,
    },
  };
}

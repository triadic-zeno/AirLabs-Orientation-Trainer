// The parts of AirLabs/TrainerCore.h the server needs, ported to JavaScript. The numbers come
// from core-data.js, which tools/dump_core.cpp writes straight from TrainerCore.h.
import data from './core-data.js';

export const C = data.constants;
export const DATA = data;
export const FLICK = 0, TRACK = 1, HYBRID = 2, CHASE = 3;
export const DT = Math.fround(1 / 120); // the plugin's tick, a float
const DEG = Math.PI / 180;

export function clamp(v, lo, hi) { return v < lo ? lo : v > hi ? hi : v; }

// Unreal rotator (65536 units per turn) to the car's axes, as in BasisFromRotator.
export function basis(pitchU, yawU, rollU, out, o) {
  const k = Math.PI / 32768;
  const p = pitchU * k, y = yawU * k, r = rollU * k;
  const cp = Math.cos(p), sp = Math.sin(p), cy = Math.cos(y), sy = Math.sin(y), cr = Math.cos(r), sr = Math.sin(r);
  out[o] = cp * cy; out[o + 1] = cp * sy; out[o + 2] = sp;                                            // forward
  out[o + 3] = sr * sp * cy - cr * sy; out[o + 4] = sr * sp * sy + cr * cy; out[o + 5] = -sr * cp;   // right
  out[o + 6] = -(cr * sp * cy + sr * sy); out[o + 7] = cy * sr - cr * sp * sy; out[o + 8] = cr * cp; // up
}

export function angleDeg(ax, ay, az, bx, by, bz) {
  const la = Math.hypot(ax, ay, az) || 1, lb = Math.hypot(bx, by, bz) || 1;
  return Math.acos(clamp((ax * bx + ay * by + az * bz) / (la * lb), -1, 1)) / DEG;
}

export function fastestTurnSec(deg) {
  const th = Math.max(0, deg) * DEG;
  const peak = Math.sqrt(C.pitchTorque * th);
  if (peak <= C.maxSpin) return 2 * Math.sqrt(th / C.pitchTorque);
  return th / C.maxSpin + C.maxSpin / C.pitchTorque;
}
export function parSec(deg) { return C.parReaction + C.parFactor * fastestTurnSec(deg); }
export function flickPoints(seconds, turnDeg) {
  const t = Math.max(0.05, seconds);
  return 1000 * clamp(parSec(turnDeg) / t, 0, 2);
}
export function trackCredit(err, target) {
  if (err <= target) return 1;
  if (err >= 2 * target) return 0;
  return (2 * target - err) / target;
}
export function ballTargetDeg(distance) {
  const d = Math.max(distance, C.targetBallRadius + 1);
  return Math.asin(C.targetBallRadius / d) / DEG;
}

export function presetList(mode) {
  return mode === FLICK ? data.flick : mode === TRACK ? data.track : mode === HYBRID ? data.hybrid : mode === CHASE ? data.chase : null;
}
export function presetCount(mode) { const l = presetList(mode); return l ? l.length : 0; }
export function presetUsesRange(mode, preset) { return !(mode === FLICK && data.flick[preset] && data.flick[preset].fixedDistance > 0); }
export function presetDistance(mode, preset, range) {
  return presetUsesRange(mode, preset) ? C.rangeDistance[range] : data.flick[preset].fixedDistance;
}
export function presetAim(mode, preset) { return mode === FLICK ? data.flick[preset].aim : 0; }

// A board is one preset at one range, with or without air roll: "mode-preset-range-noroll".
export function boardKey(mode, preset, range, noRoll) {
  return `${mode}-${preset}-${presetUsesRange(mode, preset) ? range : 0}-${noRoll ? 1 : 0}`;
}
export function parseBoard(key) {
  const m = /^(\d)-(\d{1,2})-(\d)-([01])$/.exec(String(key || ''));
  if (!m) return null;
  const b = { mode: +m[1], preset: +m[2], range: +m[3], noRoll: m[4] === '1' };
  if (b.mode > 3 || b.preset >= presetCount(b.mode) || b.range > 2) return null;
  if (!presetUsesRange(b.mode, b.preset) && b.range !== 0) return null;
  if (b.noRoll && presetAim(b.mode, b.preset) !== 0) return null; // wheels aim needs roll
  return b;
}
export function presetName(mode, preset) {
  const p = presetList(mode)[preset];
  return p.group ? `${p.group} ${p.name}` : p.name;
}
export function boardTitle(b) {
  let t = `${data.modes[b.mode]} ${presetName(b.mode, b.preset)}`;
  if (presetUsesRange(b.mode, b.preset)) t += ` · ${data.ranges[b.range]}`;
  if (b.noRoll) t += ' · No air roll';
  return t;
}
export function allBoards() {
  const out = [];
  for (let mode = 0; mode < 4; mode++)
    for (let preset = 0; preset < presetCount(mode); preset++)
      for (let range = 0; range < 3; range++) {
        if (!presetUsesRange(mode, preset) && range > 0) continue;
        out.push(boardKey(mode, preset, range, false));
        if (presetAim(mode, preset) === 0) out.push(boardKey(mode, preset, range, true));
      }
  return out;
}

export function marks(mode, preset, range) {
  return data.marks[mode][preset][presetUsesRange(mode, preset) ? range : 0];
}
export function tierIndex(score, mode, preset, range) {
  const m = marks(mode, preset, range);
  let t = 0;
  for (let i = 0; i < m.length; i++) if (score >= m[i]) t = i + 1;
  return t;
}
export function proMark(mode, preset, range) { const m = marks(mode, preset, range); return m[m.length - 1]; }

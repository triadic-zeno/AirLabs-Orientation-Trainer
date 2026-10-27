// The verifier against replays made by the plugin's own code (driving a simulated car), and
// against doctored copies of them. Nothing but a wrong preset turns a run away: the rest is
// flagged, and a claimed score the replay doesn't support is replaced by the replayed one.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, readdirSync } from 'node:fs';
import { gunzipSync } from 'node:zlib';
import { parseReplay, packReplay, unpackReplay, fromBase64, toBase64, deflate, inflate } from '../src/replay.js';
import { verifyReplay } from '../src/verify.js';

const dir = new URL('./fixtures/', import.meta.url);
export const fixtures = Object.fromEntries(readdirSync(dir).filter((f) => f.endsWith('.alr.gz'))
  .map((f) => [f.replace('.alr.gz', ''), new Uint8Array(gunzipSync(readFileSync(new URL(f, dir))))]));
const copy = (b) => new Uint8Array(b);
const view = (b) => new DataView(b.buffer, b.byteOffset, b.byteLength);
const check = (b) => verifyReplay(parseReplay(b));
const HB = 64, SAMPLE = 14;
const ns = (b) => view(b).getUint32(16, true);
const evOffset = (b) => HB + ns(b) * SAMPLE;

test('every fixture replay scores the same on the server', () => {
  assert.ok(Object.keys(fixtures).length >= 8);
  for (const [name, b] of Object.entries(fixtures)) {
    const r = parseReplay(b);
    const v = verifyReplay(r);
    assert.ok(v.ok, `${name}: ${v.reason}`);
    assert.ok(Math.abs(v.score - r.score) <= 25, `${name}: ${v.score} vs ${r.score}`);
    assert.equal(v.boardScore, r.score, name);
    assert.deepEqual(v.flags.filter((f) => f !== 'clock' && f !== 'spin'), [], `${name}: ${v.flags}`); // (the game can update the car's rotation every other tick)
  }
});

test('storage packing round-trips exactly', async () => {
  for (const b of Object.values(fixtures)) {
    const stored = toBase64(await deflate(packReplay(b)));
    assert.ok(stored.length < b.length, 'packed + deflated is smaller than raw');
    assert.deepEqual(unpackReplay(await inflate(fromBase64(stored))), b);
  }
});

test('a raised score is replaced by the replayed one', () => {
  const b = copy(fixtures['0-4-1-0']);
  const real = view(b).getInt32(40, true);
  view(b).setInt32(40, real + 3000, true);
  const v = check(b);
  assert.ok(v.ok);
  assert.equal(v.boardScore, real);
  assert.ok(v.flags.includes('score'));
});

test('claiming a bigger target (shorter range) is rejected', () => {
  const b = copy(fixtures['0-4-1-0']);
  b[10] = 0; // Medium -> Short
  assert.match(check(b).reason, /distance/);
});

test('claiming another preset is flagged', () => {
  const b = copy(fixtures['0-4-1-0']);
  b[9] = 3; // Mixed -> Turnaround (135-180 deg turns)
  assert.ok(check(b).flags.includes('turns'));
});

test('a frozen ball on Track is flagged', () => {
  const b = copy(fixtures['1-1-1-0']);
  const v = view(b);
  const x = v.getInt16(HB + 8, true), y = v.getInt16(HB + 10, true), z = v.getInt16(HB + 12, true);
  for (let i = 0; i < ns(b); i++) { v.setInt16(HB + i * SAMPLE + 8, x, true); v.setInt16(HB + i * SAMPLE + 10, y, true); v.setInt16(HB + i * SAMPLE + 12, z, true); }
  assert.ok(check(b).flags.includes('ball-speed'));
});

test('a slowed-down ball on Track is flagged', () => {
  const b = copy(fixtures['1-1-1-0']);
  const v = view(b), src = copy(b), sv = view(src);
  for (let i = 0; i < ns(b); i++) for (let c = 0; c < 3; c++) v.setInt16(HB + i * SAMPLE + 8 + c * 2, sv.getInt16(HB + (i >> 1) * SAMPLE + 8 + c * 2, true), true);
  assert.ok(check(b).flags.includes('ball-speed'));
});

test('a car turning faster than the game allows is flagged', () => {
  // Yaw at 6.9 rad/s the whole run (the game caps a car at 5.5).
  const b = copy(fixtures['3-3-0-0']);
  const v = view(b);
  for (let i = 0; i < ns(b); i++) { v.setInt16(HB + i * SAMPLE + 2, 0, true); v.setInt16(HB + i * SAMPLE + 4, (i * 600) << 16 >> 16, true); v.setInt16(HB + i * SAMPLE + 6, 0, true); }
  assert.ok(check(b).flags.includes('spin'));
});

test('a sped-up car is flagged', () => {
  // The whole run at three times the speed: some check (spin, or the events) has to notice.
  const b = copy(fixtures['3-3-0-0']);
  const v = view(b), src = copy(b), sv = view(src);
  const n = ns(b);
  for (let i = 0; i < n; i++) for (let c = 0; c < 3; c++) v.setInt16(HB + i * SAMPLE + 2 + c * 2, sv.getInt16(HB + Math.min(n - 1, i * 3) * SAMPLE + 2 + c * 2, true), true);
  const r = check(b);
  assert.ok(r.ok && r.flags.length > 0, String(r.flags));
});

test('air roll on a No air roll run is flagged', () => {
  const b = copy(fixtures['3-1-1-1']);
  const v = view(b);
  for (let i = 0; i < ns(b); i++) v.setInt16(HB + i * SAMPLE + 6, (v.getInt16(HB + i * SAMPLE + 6, true) + i * 40) << 16 >> 16, true);
  assert.ok(check(b).flags.includes('roll'));
});

test('hits claimed earlier than they happened are flagged, and scored as they happened', () => {
  const b = copy(fixtures['0-4-1-0']);
  const v = view(b), o = evOffset(b);
  const ne = v.getUint32(20, true);
  for (let k = 0; k < ne; k++) if (b[o + k * 12 + 4] === 2) v.setUint32(o + k * 12, v.getUint32(o + k * 12, true) - 15, true);
  const r = check(b);
  assert.ok(r.ok && r.flags.includes('events'), String(r.flags));
});

test('a perfect Track run goes up as it is', () => {
  // Point the car exactly at the ball every tick: physically possible on Track, but no human does it.
  const b = copy(fixtures['1-1-1-0']);
  const v = view(b);
  const toU = (rad) => Math.round(rad * 32768 / Math.PI) << 16 >> 16;
  let score = 0;
  for (let i = 0; i < ns(b); i++) {
    const o = HB + i * SAMPLE;
    const x = v.getInt16(o + 8, true) / 32767, y = v.getInt16(o + 10, true) / 32767, z = v.getInt16(o + 12, true) / 32767;
    const l = Math.hypot(x, y, z);
    v.setInt16(o + 2, toU(Math.asin(z / l)), true);
    v.setInt16(o + 4, toU(Math.atan2(y, x)), true);
    v.setInt16(o + 6, 0, true);
    score += 1000 / 120;
  }
  v.setInt32(40, Math.round(score), true);
  const res = check(b);
  assert.equal(res.ok, true);
  assert.ok(res.score > 59000, String(res.score));
});

test('a run with the wrong ball distance for its preset is turned away', () => {
  const b = copy(fixtures['0-4-1-0']);
  view(b).setFloat32(24, 450, true);
  assert.match(check(b).reason, /distance/);
});

test('broken replays are refused', () => {
  assert.throws(() => parseReplay(new Uint8Array(10)));
  const b = fixtures['0-4-1-0'];
  assert.throws(() => parseReplay(b.subarray(0, b.length - 5)));
  const c = copy(b); c[0] = 0x58;
  assert.throws(() => parseReplay(c));
});

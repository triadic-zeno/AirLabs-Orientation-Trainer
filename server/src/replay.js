// Reads an AirLabs replay ("ALR1", see AirLabs/Replay.h for the layout).
export const EV = { SPAWN: 1, HIT: 2, MISS: 3, HYB_START: 4, HYB_END: 5, CH_LAUNCH: 6, CH_STOP: 7, CH_CATCH: 8, CH_MISS: 9 };
export const RF = { NOROLL: 1, KEYBOARD: 2, TICKTIMING: 4 };
const HEADER = 64, SAMPLE = 14, EVENT = 12, MAX_SAMPLES = 120 * 75;

export function parseReplay(bytes) {
  if (!(bytes instanceof Uint8Array)) bytes = new Uint8Array(bytes);
  const fail = (m) => { throw new Error(m); };
  if (bytes.length < HEADER || String.fromCharCode(bytes[0], bytes[1], bytes[2], bytes[3]) !== 'ALR1') fail('not an AirLabs replay');
  const v = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (v.getUint16(4, true) !== 1) fail('replay format not supported');
  const hb = v.getUint16(6, true);
  if (hb < HEADER) fail('bad replay header');
  const ns = v.getUint32(16, true), ne = v.getUint32(20, true);
  if (ns > MAX_SAMPLES || ne > ns * 4 + 16) fail('replay too long');
  if (bytes.length !== hb + ns * SAMPLE + ne * EVENT) fail("replay size doesn't match");
  let version = '';
  for (let i = 52; i < 60 && bytes[i]; i++) version += String.fromCharCode(bytes[i]);
  const r = {
    mode: bytes[8], preset: bytes[9], range: bytes[10], aim: bytes[11], flags: bytes[12],
    distance: v.getFloat32(24, true), targetDeg: v.getFloat32(28, true), runSec: v.getFloat32(32, true), tickRate: v.getFloat32(36, true),
    score: v.getInt32(40, true), targets: v.getUint16(44, true), misses: v.getUint16(46, true), hybReps: v.getUint16(48, true),
    version,
    n: ns,
    ms: new Uint16Array(ns),
    rot: new Int16Array(ns * 3),
    dir: new Float64Array(ns * 3),
    events: [],
  };
  let o = hb;
  for (let i = 0; i < ns; i++, o += SAMPLE) {
    r.ms[i] = v.getUint16(o, true);
    r.rot[i * 3] = v.getInt16(o + 2, true); r.rot[i * 3 + 1] = v.getInt16(o + 4, true); r.rot[i * 3 + 2] = v.getInt16(o + 6, true);
    let x = v.getInt16(o + 8, true) / 32767, y = v.getInt16(o + 10, true) / 32767, z = v.getInt16(o + 12, true) / 32767;
    const l = Math.hypot(x, y, z) || 1;
    r.dir[i * 3] = x / l; r.dir[i * 3 + 1] = y / l; r.dir[i * 3 + 2] = z / l;
  }
  for (let i = 0; i < ne; i++, o += EVENT) r.events.push({ tick: v.getUint32(o, true), type: bytes[o + 4], value: v.getFloat32(o + 8, true) });
  return r;
}

// base64 <-> bytes without Node's Buffer (Workers have atob/btoa).
export function fromBase64(s) {
  const bin = atob(String(s).replace(/[\r\n\s]/g, '').replace(/-/g, '+').replace(/_/g, '/'));
  const out = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
  return out;
}
export function toBase64(bytes) {
  let s = '';
  for (let i = 0; i < bytes.length; i += 0x8000) s += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
  return btoa(s);
}

async function pipe(bytes, stream) {
  const out = new Response(new Blob([bytes]).stream().pipeThrough(stream));
  return new Uint8Array(await out.arrayBuffer());
}
export const deflate = (b) => pipe(b, new CompressionStream('deflate-raw'));
export const inflate = (b) => pipe(b, new DecompressionStream('deflate-raw'));

// Storage form (server side only): the samples as seven columns of tick-to-tick differences,
// low bytes then high bytes. Consecutive ticks differ by little, so this deflates to about a
// third of the raw replay. pack/unpack are exact inverses on a valid ALR1 replay.
export function packReplay(raw) {
  const v = new DataView(raw.buffer, raw.byteOffset, raw.byteLength);
  const hb = v.getUint16(6, true), ns = v.getUint32(16, true);
  const out = new Uint8Array(raw.length);
  out.set(raw.subarray(0, hb), 0);
  const lo = hb, hi = hb + ns * 7;
  for (let c = 0; c < 7; c++) {
    let prev = 0;
    for (let i = 0; i < ns; i++) {
      const val = v.getUint16(hb + i * 14 + c * 2, true);
      const d = (val - prev) & 0xFFFF;
      prev = val;
      out[lo + c * ns + i] = d & 0xFF;
      out[hi + c * ns + i] = d >> 8;
    }
  }
  out.set(raw.subarray(hb + ns * 14), hb + ns * 14);
  return out;
}
export function unpackReplay(p) {
  const v = new DataView(p.buffer, p.byteOffset, p.byteLength);
  const hb = v.getUint16(6, true), ns = v.getUint32(16, true);
  const out = new Uint8Array(p.length);
  const w = new DataView(out.buffer);
  out.set(p.subarray(0, hb), 0);
  const lo = hb, hi = hb + ns * 7;
  for (let c = 0; c < 7; c++) {
    let prev = 0;
    for (let i = 0; i < ns; i++) {
      prev = (prev + (p[lo + c * ns + i] | (p[hi + c * ns + i] << 8))) & 0xFFFF;
      w.setUint16(hb + i * 14 + c * 2, prev, true);
    }
  }
  out.set(p.subarray(hb + ns * 14), hb + ns * 14);
  return out;
}

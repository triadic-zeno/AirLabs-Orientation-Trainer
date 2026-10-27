// Verifies replay files: node test/check.mjs file.alr [...]
import { readFileSync } from 'node:fs';
import { parseReplay } from '../src/replay.js';
import { verifyReplay } from '../src/verify.js';
import { boardKey } from '../src/core.js';
for (const f of process.argv.slice(2)) {
  const r = parseReplay(readFileSync(f));
  const t0 = performance.now();
  const v = verifyReplay(r);
  const ms = performance.now() - t0;
  console.log(f.split('/').pop(), boardKey(r.mode, r.preset, r.range, r.flags & 1), 'claimed', r.score,
    v.ok ? `OK replayed ${v.score} (${(v.score - r.score)}) ${JSON.stringify(v.stats)} ${v.flags.join(',')}` : 'REJECTED: ' + v.reason, `${ms.toFixed(1)} ms`);
}

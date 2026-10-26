// Runs the Worker in Node on http://localhost:8787 with a SQLite file standing in for D1.
//   node test/localserver.mjs [db-file] [port]
// Used to test the plugin against the real server code without deploying.
import http from 'node:http';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import worker from '../src/index.js';
import { makeD1 } from './d1shim.mjs';

const here = dirname(fileURLToPath(import.meta.url));
const env = { DB: makeD1(process.argv[2] || ':memory:', join(here, '..', 'migrations')) };
const port = +(process.argv[3] || 8787);

http.createServer(async (req, res) => {
  const chunks = [];
  for await (const c of req) chunks.push(c);
  const body = Buffer.concat(chunks);
  const r = new Request('http://localhost:' + port + req.url, {
    method: req.method, headers: { ...req.headers, 'cf-connecting-ip': req.socket.remoteAddress },
    body: req.method === 'GET' || req.method === 'HEAD' ? undefined : body,
  });
  const out = await worker.fetch(r, env);
  res.writeHead(out.status, Object.fromEntries(out.headers));
  res.end(Buffer.from(await out.arrayBuffer()));
  console.log(req.method, req.url, out.status);
}).listen(port, () => console.log('AirLabs leaderboard on http://localhost:' + port));

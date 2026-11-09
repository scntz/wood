// Runs the playground examples through the browser's compiler and runtime, and through native
// wood, and requires the same stdout, status and error positions. Then checks the sandbox limits.
// usage: node examples.mjs WOOD DIST
import { spawnSync } from 'node:child_process';
import { mkdtempSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { tmpdir } from 'node:os';
import { pathToFileURL } from 'node:url';

const repository = resolve(import.meta.dirname, '../..');
const { Sandbox, Files, compileSource, runProgram, runtimeLayout } = await import(pathToFileURL(join(repository, 'tools/playground/sandbox.js')));
const [wood, dist] = process.argv.slice(2);
const runtime = new WebAssembly.Module(readFileSync(`${dist}/wood-runtime.wasm`));
const compiler = new WebAssembly.Module(readFileSync(`${dist}/wood-compiler.wasm`));
const assets = { stdlib: readFileSync(`${dist}/stdlib.json`), platform: readFileSync(`${dist}/wasm64-browser.ll`), core: readFileSync(`${dist}/core.ll`) };
const layout = await runtimeLayout(runtime);
const scratch = mkdtempSync(join(tmpdir(), 'wood-playground-'));
const bare = (text) => text.split(String.fromCharCode(13)).join('');

async function inBrowser(code) {
  const compiled = await compileSource(compiler, assets, new TextEncoder().encode(code), layout);
  if (!compiled.ok) return { status: compiled.status, stdout: '', stderr: compiled.diagnostics, limit: compiled.limit };
  let stdout = '', stderr = '';
  const sandbox = new Sandbox({ files: new Files(), onOutput: (stream, text) => { if (stream === 'stdout') stdout += text; else stderr += text; } });
  const result = await runProgram(runtime, new WebAssembly.Module(compiled.bytes), sandbox);
  return { status: result.status, stdout, stderr, limit: result.limit };
}
let count = 0;
function natively(code) {
  const file = join(scratch, `p${count += 1}.wd`);
  writeFileSync(file, code);
  // In the scratch folder: programs write files, and those must not land in the repository.
  const done = spawnSync(wood, ['run', file, '--diagnostics=json'], { encoding: 'utf8', timeout: 60000, input: '', cwd: scratch });
  return { status: done.status, stdout: bare(done.stdout), stderr: done.stderr };
}
const positions = (text) => text.split('\n').filter((line) => line.startsWith('{')).map((line) => { const d = JSON.parse(line); return `${d.code}:${d.line}:${d.column}`; }).join(' ');
const nativePositions = (text) => [...text.matchAll(/^(?:.*):(\d+):(\d+): error\[(\w+)\]/gm)].map((m) => `${m[3]}:${m[1]}:${m[2]}`).concat(text.split('\n').filter((line) => line.startsWith('{')).map((line) => { const d = JSON.parse(line); return `${d.code}:${d.line}:${d.column}`; })).join(' ');

const failures = [];
const fail = (where, detail) => failures.push(`${where}: ${detail}`);
let programs = 0;

async function same(where, code) {
  programs += 1;
  const a = natively(code), b = await inBrowser(code);
  if (a.stdout !== b.stdout) fail(where, `stdout differs\nnative: ${a.stdout}\nbrowser: ${b.stdout}\n${b.stderr}`);
  if (a.status !== b.status) fail(where, `status native ${a.status}, browser ${b.status} ${b.limit ?? ''}\n${a.stderr}\n${b.stderr}`);
  const p = nativePositions(a.stderr), q = positions(b.stderr);
  if (p !== q) fail(where, `diagnostics differ: native ${p} browser ${q}`);
  return b;
}

const samples = join(repository, 'tools/playground/examples');
for (const name of readdirSync(samples).sort()) {
  const result = await same(`playground/${name}`, readFileSync(join(samples, name), 'utf8'));
  if (result.status !== 0) fail(`playground/${name}`, 'does not run cleanly');
}

// Sandbox limits and refusals, which only the browser path has.
const limited = async (code, wanted, where) => {
  programs += 1;
  const result = await inBrowser(code);
  if (!wanted(result)) fail(where, JSON.stringify({ status: result.status, limit: result.limit, stderr: result.stderr.slice(0, 200) }));
};
await limited('while true:\n    print("spam spam spam spam spam spam")\n', (r) => r.limit === 'output', 'output limit');
await limited('fn f(n: int) -> int:\n    return f(n + 1) + 1\nprint(f(0))\n', (r) => r.limit === 'stack', 'stack limit');
await limited('items = [1]\nwhile true:\n    items = items + items\n', (r) => r.limit || r.status === 70, 'memory limit');
await limited('import std.fs as fs\nprint(fs.kind("/etc/passwd"))\n', (r) => r.status === 0 && r.stdout.trim() === 'missing', 'no host files');
await limited('write_text("note.txt", "kept")\nprint(read_text("note.txt"))\n', (r) => r.status === 0 && r.stdout.trim() === 'kept', 'scratch files');
// The documented limits themselves, not merely that some limit exists.
const quarter = 'x = "' + 'y'.repeat(1024) + '"\nblock = x * 1024\n';
await limited(`${quarter}for i in range(17):\n    write_text("f" + str(i) + ".bin", block)\nprint("wrote")\n`, (r) => r.status !== 0 && r.stdout.trim() === '', 'files stop at 16 MB');
await limited(`${quarter}for i in range(15):\n    write_text("f" + str(i) + ".bin", block)\nprint("wrote")\n`, (r) => r.status === 0 && r.stdout.trim() === 'wrote', 'files fit under 16 MB');
await limited(`${quarter}for i in range(100):\n    write_text("same.bin", block)\nprint(len(read_text("same.bin")))\n`, (r) => r.status === 0 && r.stdout.trim() === '1048576', 'rewriting a file does not use up the limit');
await limited('import std.fs as fs\n' + `${quarter}for i in range(100):\n    write_text("same.bin", block)\n    fs.remove("same.bin")\nprint("ok")\n`, (r) => r.status === 0 && r.stdout.trim() === 'ok', 'removing a file frees its space');
await limited('text = "x" * 1048000\nprint(text)\n', (r) => r.status === 0 && r.stdout.length === 1048001, 'output just under 1 MB is kept');
await limited('text = "x" * 1048577\nprint(text)\n', (r) => r.limit === 'output', 'output over 1 MB stops');
await limited('items = [0] * 10000000\nprint(len(items))\n', (r) => r.status === 0 && r.stdout.trim() === '10000000', '80 MB of memory is allowed');
await limited('items = [0] * 40000000\nprint(len(items))\n', (r) => r.status !== 0, '320 MB of memory is not');
await limited('import std.env as env\nprint(env.get("PATH"))\n', (r) => r.status === 0 && r.stdout.trim() === 'None', 'empty environment');
await limited('import std.time as time\nstart = time.perf_counter_ns()\ntime.sleep(0.02)\nprint(time.perf_counter_ns() - start >= 20000000)\n', (r) => r.status === 0 && r.stdout.trim() === 'true', 'sleep waits');
for (const [module, call] of [['net', 'connect("example.com", 80, 100)'], ['tls', 'connect("example.com", 443, "", 100)'], ['sqlite', 'open("x.db", 100)'], ['python', 'import_module("math")']]) {
  await limited(`import std.${module} as m
try:
    m.${call}
except e:
    print("caught")
`, (r) => r.status === 0 && r.stdout.trim() === 'caught', `${module} raises an ordinary error`);
}

rmSync(scratch, { recursive: true, force: true });
for (const failure of failures) console.log(`FAIL ${failure}`);
console.log(`playground: ${programs} programs, ${failures.length} failures`);
process.exitCode = failures.length ? 1 : 0;

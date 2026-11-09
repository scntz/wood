// Runs each program natively and in the browser runtime, and requires the same stdout and exit
// status. stderr is compared only for diagnostics' code and position, since paths differ.
// usage: node differential.mjs WOOD DIST [--native-lowering TOOL STDLIB] FILES_OR_DIRECTORIES...
import { execFileSync, spawnSync } from 'node:child_process';
import { mkdtempSync, readFileSync, readdirSync, rmSync, statSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { tmpdir } from 'node:os';
import { pathToFileURL } from 'node:url';

const sandboxFile = resolve(import.meta.dirname, '../../tools/playground/sandbox.js');
const { Sandbox, Files, compileSource, runProgram, runtimeLayout } = await import(pathToFileURL(sandboxFile));

const args = process.argv.slice(2);
const wood = args.shift();
const dist = args.shift();
let tool = null, stdlibFile = null, nativeDir = resolve(import.meta.dirname, '../../runtime/native');
if (args[0] === '--native-lowering') { args.shift(); tool = args.shift(); stdlibFile = args.shift(); }

const files = [];
const walk = (path) => {
  if (statSync(path).isDirectory()) for (const name of readdirSync(path).sort()) walk(join(path, name));
  else if (path.endsWith('.wd')) files.push(path);
};
args.forEach(walk);

const runtime = new WebAssembly.Module(readFileSync(`${dist}/wood-runtime.wasm`));
const layout = await runtimeLayout(runtime);
const compiler = tool ? null : new WebAssembly.Module(readFileSync(`${dist}/wood-compiler.wasm`));
const assets = tool ? null : { stdlib: readFileSync(`${dist}/stdlib.json`), platform: readFileSync(`${dist}/wasm64-browser.ll`), core: readFileSync(`${dist}/core.ll`) };
const scratch = mkdtempSync(join(tmpdir(), 'wood-playground-'));

const code = (text) => { try { return text.trim().split('\n').map((line) => JSON.parse(line)).map((d) => `${d.code}:${d.line}:${d.column}`).join(' '); } catch { return null; } };

let passed = 0, failed = 0, skipped = 0;
const failures = [];
for (const file of files) {
  const first = readFileSync(file, 'utf8');
  if (/\bimport\s+(std\.(net|http|https|tls|sqlite|python|task)|python@)|\bfrom\s+std\.(net|http|https|tls|sqlite|python|task)\b/.test(first) || /\b(args|input|read_line)\(/.test(first) && !/playground: run/.test(first)) { skipped += 1; continue; }
  const native = spawnSync(resolve(wood), ['run', resolve(file), '--diagnostics=json'], { encoding: 'utf8', timeout: 60000, input: '', cwd: scratch });
  if (native.status === null) { skipped += 1; continue; }
  if (native.status === 2) { skipped += 1; continue; }
  let stdout = '', stderr = '', result;
  try {
    let bytes;
    if (tool) {
      const out = join(scratch, `p${passed + failed}.wasm`);
      const made = spawnSync(tool, ['wasm', stdlibFile, nativeDir, file, out, String(layout.memoryBase), String(layout.tableBase)], { encoding: 'utf8' });
      if (made.status !== 0) { result = { status: made.status }; stderr = made.stderr; }
      else bytes = readFileSync(out);
    } else {
      const compiled = await compileSource(compiler, assets, readFileSync(file), layout);
      if (!compiled.ok) { result = { status: compiled.status, limit: compiled.limit, detail: compiled.detail }; stderr = compiled.diagnostics; }
      else bytes = compiled.bytes;
    }
    if (bytes) {
      const sandbox = new Sandbox({ files: new Files(), onOutput: (stream, text) => { if (stream === 'stdout') stdout += text; else stderr += text; } });
      result = await runProgram(runtime, new WebAssembly.Module(bytes), sandbox);
    }
  } catch (error) {
    result = { status: -1, detail: String(error.stack ?? error) };
  }
  const nativeStderr = code(native.stderr ?? ''), wasmStderr = code(stderr);
  native.stdout = native.stdout.split(String.fromCharCode(13)).join('');
  const same = native.stdout === stdout && native.status === result.status && (nativeStderr === wasmStderr || nativeStderr === null);
  if (same) passed += 1;
  else {
    failed += 1;
    failures.push({ file, native: native.status, wasm: result.status, limit: result.limit, detail: (result.trace ?? result.detail ?? '').slice(0, 400), stdoutSame: native.stdout === stdout, nativeStderr: (native.stderr ?? '').slice(0, 200), wasmStderr: stderr.slice(0, 200), nativeOut: native.stdout.slice(-120), wasmOut: stdout.slice(-120) });
  }
}
rmSync(scratch, { recursive: true, force: true });
for (const item of failures) console.log(JSON.stringify(item));
console.log(`differential: ${passed} same, ${failed} different, ${skipped} skipped, ${files.length} programs`);
process.exitCode = failed ? 1 : 0;

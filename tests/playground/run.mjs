// Compiles and runs one wood file through the WebAssembly compiler and runtime under Node.
// usage: node run.mjs DIST SOURCE.wd [--stdin TEXT] [--native-lowering FILE.wasm]
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

const [dist, sourcePath, ...rest] = process.argv.slice(2);
const home = resolve(import.meta.dirname, '../../tools/playground/sandbox.js');
const { Sandbox, Files, compileSource, runProgram, runtimeLayout } = await import(pathToFileURL(home));

let stdin = '';
let prepared = null;
for (let i = 0; i < rest.length; i += 2) {
  if (rest[i] === '--stdin') stdin = rest[i + 1];
  else if (rest[i] === '--native-lowering') prepared = rest[i + 1];
}

const runtime = new WebAssembly.Module(readFileSync(`${dist}/wood-runtime.wasm`));
const layout = await runtimeLayout(runtime);
let bytes;
if (prepared) {
  bytes = readFileSync(prepared);
} else {
  const compiler = new WebAssembly.Module(readFileSync(`${dist}/wood-compiler.wasm`));
  const assets = {
    stdlib: readFileSync(`${dist}/stdlib.json`),
    platform: readFileSync(`${dist}/wasm64-browser.ll`),
    core: readFileSync(`${dist}/core.ll`),
  };
  const started = performance.now();
  const compiled = await compileSource(compiler, assets, readFileSync(sourcePath), layout);
  if (process.env.WOOD_PLAYGROUND_TIMING) process.stderr.write(`compile ${Math.round(performance.now() - started)} ms\n`);
  if (!compiled.ok) {
    process.stderr.write(compiled.diagnostics);
    if (compiled.limit) process.stderr.write(`compiler stopped: ${compiled.limit} ${compiled.detail ?? ''}\n`);
    process.exit(compiled.status === 0 ? 1 : compiled.status);
  }
  bytes = compiled.bytes;
}
const program = new WebAssembly.Module(bytes);
const sandbox = new Sandbox({
  files: new Files(),
  stdin,
  onOutput: (stream, text) => (stream === 'stdout' ? process.stdout : process.stderr).write(text),
});
const result = await runProgram(runtime, program, sandbox);
if (result.limit) process.stderr.write(`stopped: ${result.limit} ${result.detail ?? ''}\n`);
process.exitCode = result.status;

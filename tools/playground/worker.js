// Compiles and runs one program at a time, away from the page. The page ends this worker
// when a program runs too long, which is why it is never trusted with anything else.
import { Files, Sandbox, compileSource, runProgram, runtimeLayout } from './sandbox.js';

const base = new URL('./', import.meta.url);

async function module(name) {
  const response = await fetch(new URL(name, base));
  if (!response.ok) throw new Error(`${name}: ${response.status}`);
  return WebAssembly.compile(await response.arrayBuffer());
}
async function bytes(name) {
  const response = await fetch(new URL(name, base));
  if (!response.ok) throw new Error(`${name}: ${response.status}`);
  return new Uint8Array(await response.arrayBuffer());
}

const ready = (async () => {
  const [compiler, runtime, stdlib, platform, core] = await Promise.all([
    module('wood-compiler.wasm'), module('wood-runtime.wasm'), bytes('stdlib.json'), bytes('wasm64-browser.ll'), bytes('core.ll'),
  ]);
  return { compiler, runtime, assets: { stdlib, platform, core }, layout: await runtimeLayout(runtime) };
})();

ready.then(() => postMessage({ type: 'ready' }), (error) => postMessage({ type: 'unavailable', detail: String(error) }));

const encoder = new TextEncoder();

self.onmessage = async ({ data }) => {
  const { id, source } = data;
  try {
    const { compiler, runtime, assets, layout } = await ready;
    const compiled = await compileSource(compiler, assets, encoder.encode(source), layout);
    if (!compiled.ok) {
      postMessage({ type: 'done', id, stage: 'compile', status: compiled.status, diagnostics: compiled.diagnostics, limit: compiled.limit });
      return;
    }
    postMessage({ type: 'running', id });
    // Output is sent in small batches, so a chatty program cannot flood the page.
    let pending = [], timer = 0, stderr = '';
    const send = () => { timer = 0; if (pending.length) postMessage({ type: 'output', id, chunks: pending }); pending = []; };
    const sandbox = new Sandbox({
      files: new Files(),
      onOutput: (stream, text) => {
        if (stream === 'stderr') stderr += text;
        pending.push([stream, text]);
        if (!timer) timer = setTimeout(send, 30);
      },
    });
    const result = await runProgram(runtime, new WebAssembly.Module(compiled.bytes), sandbox, { maximumPages: 4096 });
    if (timer) clearTimeout(timer);
    send();
    postMessage({ type: 'done', id, stage: 'run', status: result.status, limit: result.limit, stderr });
  } catch (error) {
    postMessage({ type: 'done', id, stage: 'internal', status: -1, detail: String(error) });
  }
};

// Mutation fuzzer for the compiler: wood check must answer with a diagnostic, never crash or hang, and what
// it accepts must build. Any other result, or one past the time limit, keeps its input in OUTPUT for rerunning.
// usage: node compiler.mjs WOOD SEED SECONDS OUTPUT FILES_OR_DIRECTORIES...
import { spawn } from 'node:child_process';
import { mkdirSync, readFileSync, readdirSync, statSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';

const [wood, seedText, secondsText, output, ...roots] = process.argv.slice(2);
if (!roots.length) { console.error('usage: node compiler.mjs WOOD SEED SECONDS OUTPUT FILES...'); process.exit(2); }
const deadline = Date.now() + Number(secondsText) * 1000;
// The limits catch hangs, not slowness. The planned 50,000-line input takes about 24 s with three workers running
// on an idle machine, so it has more room.
const TIME_LIMIT = 30000, PLANNED_TIME_LIMIT = 120000, WORKERS = 3;
mkdirSync(output, { recursive: true });

const corpus = [];
const walk = (path) => {
  if (statSync(path).isDirectory()) for (const name of readdirSync(path).sort()) walk(join(path, name));
  else if (path.endsWith('.wd') && statSync(path).size < 65536) corpus.push(readFileSync(path));
};
roots.forEach(walk);

let state = Number(seedText) >>> 0 || 1;
const next = (bound) => { state ^= state << 13; state >>>= 0; state ^= state >>> 17; state ^= state << 5; state >>>= 0; return state % bound; };
const tokens = ['fn ', 'record ', 'if ', 'elif ', 'else:', 'while ', 'for ', ' in ', 'return ', 'import ', 'from ', 'try:', 'except e:', 'raise ', 'with ', ' as ', 'mut ', 'extern ', 'None', 'true',
  '->', '=>', ':', '=', '==', '|', '(', ')', '[', ']', '{', '}', ',', '.', '"', '\\', '\n', '\n    ', '\t', '#', '@', '*', '/', '//', '<<', '0', '9223372036854775807', '1e999', '.5', 'list[', 'dict[', 'tuple[',
  'step[', 'array[', 'json', 'void', 'self', 'int', 'str', 'float', 'bool', '\xff', '\0', 'std.', 'python@', '[T]', 'yielded(', 'finished()'];
const text = (value) => Buffer.from(value, 'latin1');
// Deep or huge programs that mutation alone would rarely reach. Each runs once, first.
const extreme = [
  '('.repeat(100000), '['.repeat(100000), '{'.repeat(100000), `x = ${'-'.repeat(200000)}1\n`, `x = ${'not '.repeat(100000)}true\n`, `x = 1${' + 1'.repeat(100000)}\n`,
  `x = a${'.b'.repeat(100000)}\n`, `x = a${'[0]'.repeat(100000)}\n`, `x = f${'()'.repeat(100000)}\n`, `${'a'.repeat(100000)}\n`, `x = "${'a'.repeat(5000000)}"\n`,
  Array.from({ length: 2000 }, (_, i) => `${' '.repeat(i)}if true:\n`).join(''), `x: ${'list['.repeat(5000)}int${']'.repeat(5000)} = []\n`,
  `fn f(${Array.from({ length: 20000 }, (_, i) => `p${i}: int`).join(', ')}):\n    return\n`, Array.from({ length: 50000 }, (_, i) => `v${i} = ${i}\n`).join(''), `x = [${'1,'.repeat(500000)}1]\n`,
  'fn f() -> int:\n    return f\n', 'fn f(x):\n    return g(x)\nfn g(x):\n    return f(x)\nprint(f(1))\n', Array.from({ length: 3000 }, (_, i) => `fn f${i}(x):\n    return f${i + 1}(x)\n`).join(''),
  'record A:\n    b: B\nrecord B:\n    a: A\n', '\0'.repeat(100000), '\xff'.repeat(100000), `f = fn(a: int) => ${'fn(b: int) => '.repeat(2000)}a\n`,
  'record A[T]:\n    v: T\nfn f[T](x: T) -> int:\n    return f[A[T]](A(x))\nprint(f(1))\n', `x = ${'(1,'.repeat(20000)}1${')'.repeat(20000)}\n`, `x = {${'"a":{'.repeat(20000)}${'}'.repeat(20000)}}\n`,
  `if ${'true and '.repeat(100000)}true:\n    pass\n`, `x = [${'[i for i in '.repeat(3000)}[1]${']'.repeat(3000)}]\n`, Array.from({ length: 5000 }, (_, i) => `record R${i}:\n    v: R${i + 1}\n`).join(''),
].map(text);

function mutate(source) {
  let data = Buffer.from(source);
  for (let round = 1 + next(4); round > 0; round -= 1) {
    const at = next(data.length + 1), span = 1 + next(64);
    switch (next(7)) {
      case 0: if (data.length) data[Math.min(at, data.length - 1)] = next(256); break;
      case 1: data = Buffer.concat([data.subarray(0, at), data.subarray(at + span)]); break;
      case 2: data = Buffer.concat([data.subarray(0, at), text(tokens[next(tokens.length)]), data.subarray(at)]); break;
      case 3: data = Buffer.concat([data.subarray(0, at), data.subarray(at, at + span), data.subarray(at)]); break;
      case 4: { const other = corpus[next(corpus.length)], from = next(other.length + 1); data = Buffer.concat([data.subarray(0, at), other.subarray(from, from + span * 8), data.subarray(at)]); break; }
      case 5: data = data.subarray(0, at); break;
      default: { const lines = data.toString('latin1').split('\n'), a = next(lines.length), b = next(lines.length); [lines[a], lines[b]] = [lines[b], lines[a]]; data = text(lines.join('\n')); }
    }
  }
  return data;
}

let runs = 0, accepted = 0, rejected = 0, extremeIndex = 0;
const crashes = [];
const invoke = (command, limit) => new Promise((done) => {
  const child = spawn(wood, command, { stdio: ['ignore', 'ignore', 'pipe'] });
  let stderr = '', timedOut = false;
  const timer = setTimeout(() => { timedOut = true; child.kill('SIGKILL'); }, limit);
  child.stderr.on('data', (piece) => { if (stderr.length < 4096) stderr += piece; });
  child.on('close', (status, signal) => { clearTimeout(timer); done({ status, signal, stderr, timedOut }); });
});
async function worker(index) {
  const file = resolve(output, `current-${index}.wd`);
  while (Date.now() < deadline) {
    const planned = extremeIndex < extreme.length;
    const data = planned ? extreme[extremeIndex++] : mutate(corpus[next(corpus.length)]);
    writeFileSync(file, data);
    const limit = planned ? PLANNED_TIME_LIMIT : TIME_LIMIT;
    let result = await invoke(['check', file], limit);
    runs += 1;
    let failed = result.timedOut || result.signal || ![0, 1, 2].includes(result.status);
    if (result.status === 0) {
      accepted += 1;
      result = await invoke(['build', file, '-O0', '-o', resolve(output, `current-${index}.exe`)], limit);
      // A program with extern fn needs its native library, which no input here supplies.
      failed = result.status !== 0 && !(data.includes('extern') && result.stderr.includes('undefined'));
    } else if (result.status === 1 || result.status === 2) rejected += 1;
    if (failed) {
      const kept = resolve(output, `crash-${crashes.length}.wd`);
      crashes.push({ kept, planned, head: data.toString('latin1', 0, 40), status: result.status, signal: result.signal, timedOut: result.timedOut, stderr: result.stderr.slice(0, 300) });
      writeFileSync(kept, data);
    }
  }
}
await Promise.all(Array.from({ length: WORKERS }, (_, index) => worker(index)));
for (const crash of crashes) console.log(JSON.stringify(crash));
console.log(`fuzz compiler: ${runs} inputs, ${accepted} accepted, ${rejected} rejected, ${crashes.length} crashes`);
process.exitCode = crashes.length ? 1 : 0;

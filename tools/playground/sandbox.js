// Runs WebAssembly built from wood with one import: the Linux x86-64 system calls the native
// runtime already makes. Anything not implemented here answers ENOSYS, which is the sandbox:
// no network, no real filesystem, nothing outside this file. Shared by the worker and tests.

const ENOENT = 2n, EBADF = 9n, ENOMEM = 12n, EACCES = 13n, EEXIST = 17n, ENOTDIR = 20n, EISDIR = 21n, EINVAL = 22n, EFBIG = 27n, ENOSYS = 38n, ERANGE = 34n;

export class ExitSignal extends Error {
  constructor(code) { super(`exit ${code}`); this.code = code; }
}
export class LimitSignal extends Error {
  constructor(kind) { super(kind); this.kind = kind; }
}

const encoder = new TextEncoder();

// One run's files: an in-memory directory tree, thrown away with the run.
export class Files {
  constructor(entries = {}, limit = 16 * 1024 * 1024) {
    this.files = new Map();
    this.directories = new Set(['/', '/work', '/tmp']);
    this.limit = limit;
    this.used = 0;
    for (const [path, bytes] of Object.entries(entries)) this.files.set(path, typeof bytes === 'string' ? encoder.encode(bytes) : bytes);
  }
  normal(path) {
    const parts = [];
    for (const part of (path.startsWith('/') ? path : `/work/${path}`).split('/')) {
      if (part === '' || part === '.') continue;
      if (part === '..') parts.pop(); else parts.push(part);
    }
    return `/${parts.join('/')}`;
  }
  parent(path) { return path.slice(0, path.lastIndexOf('/')) || '/'; }
  list(path) {
    const prefix = path === '/' ? '/' : `${path}/`, names = new Set();
    for (const name of [...this.files.keys(), ...this.directories]) {
      if (name !== path && name.startsWith(prefix)) names.add(name.slice(prefix.length).split('/')[0]);
    }
    return [...names];
  }
}

export class Sandbox {
  constructor({ files = new Files(), stdin = '', onOutput = () => {}, outputLimit = 1024 * 1024, seed = null } = {}) {
    this.files = files;
    this.stdin = encoder.encode(stdin);
    this.stdinAt = 0;
    this.onOutput = onOutput;
    this.outputLimit = outputLimit;
    this.outputUsed = 0;
    this.descriptors = new Map();
    this.nextDescriptor = 3;
    this.memory = null;
    this.seed = seed;
    this.decoder = [new TextDecoder(), new TextDecoder()];
  }
  bytes(address, size) { return new Uint8Array(this.memory.buffer, Number(address), Number(size)); }
  text(address) {
    const memory = new Uint8Array(this.memory.buffer), start = Number(address);
    let end = start;
    while (memory[end] !== 0) end += 1;
    return new TextDecoder().decode(memory.subarray(start, end));
  }
  write(descriptor, address, size) {
    if (descriptor === 1n || descriptor === 2n) {
      this.outputUsed += Number(size);
      if (this.outputUsed > this.outputLimit) throw new LimitSignal('output');
      const stream = Number(descriptor) - 1;
      const chunk = this.decoder[stream].decode(this.bytes(address, size), { stream: true });
      if (chunk) this.onOutput(descriptor === 1n ? 'stdout' : 'stderr', chunk);
      return size;
    }
    const open = this.descriptors.get(descriptor);
    if (!open || !open.writable) return -EBADF;
    const count = Number(size), end = open.at + count;
    let content = this.files.files.get(open.path);
    // The limit is on what is stored, so rewriting a small file many times never reaches it.
    const growth = Math.max(0, end - content.length);
    if (this.files.used + growth > this.files.limit) return -EFBIG;
    if (growth) {
      const grown = new Uint8Array(end);
      grown.set(content);
      content = grown;
      this.files.files.set(open.path, content);
      this.files.used += growth;
    }
    content.set(this.bytes(address, count), open.at);
    open.at = end;
    return size;
  }
  read(descriptor, address, size) {
    if (descriptor === 0n) {
      const count = Math.min(Number(size), this.stdin.length - this.stdinAt);
      this.bytes(address, count).set(this.stdin.subarray(this.stdinAt, this.stdinAt + count));
      this.stdinAt += count;
      return BigInt(count);
    }
    const open = this.descriptors.get(descriptor);
    if (!open) return -EBADF;
    const content = this.files.files.get(open.path), count = Math.min(Number(size), content.length - open.at);
    if (count <= 0) return 0n;
    this.bytes(address, count).set(content.subarray(open.at, open.at + count));
    open.at += count;
    return BigInt(count);
  }
  open(address, flags) {
    const path = this.files.normal(this.text(address)), files = this.files;
    const access = flags & 3n, create = (flags & 64n) !== 0n, exclusive = (flags & 128n) !== 0n, truncate = (flags & 512n) !== 0n, append = (flags & 1024n) !== 0n, directory = (flags & 65536n) !== 0n;
    if (files.directories.has(path)) {
      if (access !== 0n) return -EISDIR;
      const id = BigInt(this.nextDescriptor++);
      this.descriptors.set(id, { path, directory: true, at: 0, names: files.list(path) });
      return id;
    }
    if (directory) return files.files.has(path) ? -ENOTDIR : -ENOENT;
    if (!files.files.has(path)) {
      if (!create) return -ENOENT;
      if (!files.directories.has(files.parent(path))) return -ENOENT;
      files.files.set(path, new Uint8Array(0));
    } else if (create && exclusive) return -EEXIST;
    else if (truncate && access !== 0n) { files.used -= files.files.get(path).length; files.files.set(path, new Uint8Array(0)); }
    const id = BigInt(this.nextDescriptor++);
    this.descriptors.set(id, { path, writable: access !== 0n, at: append ? files.files.get(path).length : 0 });
    return id;
  }
  stat(address, target) {
    const path = this.files.normal(this.text(address));
    const isDirectory = this.files.directories.has(path);
    if (!isDirectory && !this.files.files.has(path)) return -ENOENT;
    const view = new DataView(this.memory.buffer, Number(target), 144);
    new Uint8Array(this.memory.buffer, Number(target), 144).fill(0);
    view.setUint32(24, isDirectory ? 0o40755 : 0o100644, true);
    view.setBigInt64(48, BigInt(isDirectory ? 0 : this.files.files.get(path).length), true);
    return 0n;
  }
  directoryEntries(descriptor, address, capacity) {
    const open = this.descriptors.get(descriptor);
    if (!open || !open.directory) return -EBADF;
    if (open.at >= open.names.length) return 0n;
    const name = encoder.encode(open.names[open.at]), size = (19 + name.length + 1 + 7) & ~7;
    if (size > Number(capacity)) return -EINVAL;
    const view = new DataView(this.memory.buffer, Number(address), size);
    view.setBigUint64(0, BigInt(open.at + 1), true);
    view.setBigInt64(8, BigInt(open.at + 1), true);
    view.setUint16(16, size, true);
    view.setUint8(18, this.files.directories.has(`${open.path === '/' ? '' : open.path}/${open.names[open.at]}`) ? 4 : 8);
    new Uint8Array(this.memory.buffer, Number(address) + 19, name.length + 1).set([...name, 0]);
    open.at += 1;
    return BigInt(size);
  }
  random(address, size) {
    const target = this.bytes(address, size);
    if (this.seed !== null) {
      for (let i = 0; i < target.length; i += 1) { this.seed = (this.seed * 1103515245 + 12345) >>> 0; target[i] = this.seed >>> 16; }
    } else crypto.getRandomValues(target);
    return size;
  }
  syscall(number, a, b, c, d) {
    switch (Number(number)) {
      case 0: return this.read(a, b, c);
      case 1: return this.write(a, b, c);
      case 3: if (a > 2n && !this.descriptors.delete(a)) return -EBADF; return 0n;
      case 8: {
        const open = this.descriptors.get(a);
        if (!open) return -EBADF;
        const size = this.files.files.get(open.path).length, origin = c === 0n ? 0 : c === 1n ? open.at : size, at = origin + Number(b);
        if (at < 0) return -EINVAL;
        open.at = at;
        return BigInt(at);
      }
      case 4: return this.stat(a, b);
      case 13: case 14: return 0n;
      case 35: {
        // A worker cannot block without shared memory. Stop and the time limit still end this wait.
        const view = new DataView(this.memory.buffer, Number(a), 16);
        const end = performance.now() + Number(view.getBigInt64(0, true)) * 1000 + Number(view.getBigInt64(8, true)) / 1000000;
        while (performance.now() < end);
        return 0n;
      }
      case 79: {
        const path = encoder.encode('/work\0');
        if (Number(b) < path.length) return -ERANGE;
        this.bytes(a, path.length).set(path);
        return BigInt(path.length);
      }
      case 83: {
        const path = this.files.normal(this.text(a));
        if (this.files.directories.has(path) || this.files.files.has(path)) return -EEXIST;
        if (!this.files.directories.has(this.files.parent(path))) return -ENOENT;
        this.files.directories.add(path);
        return 0n;
      }
      case 84: {
        const path = this.files.normal(this.text(a));
        if (!this.files.directories.has(path)) return this.files.files.has(path) ? -ENOTDIR : -ENOENT;
        if (this.files.list(path).length) return -39n;
        this.files.directories.delete(path);
        return 0n;
      }
      case 87: {
        const path = this.files.normal(this.text(a));
        if (this.files.directories.has(path)) return -EISDIR;
        if (!this.files.files.has(path)) return -ENOENT;
        this.files.used -= this.files.files.get(path).length;
        this.files.files.delete(path);
        return 0n;
      }
      case 186: return 1n;
      case 202: return 0n;
      case 217: return this.directoryEntries(a, b, c);
      case 228: {
        const now = a === 0n ? Date.now() : (typeof performance !== 'undefined' ? performance.now() : Date.now());
        const view = new DataView(this.memory.buffer, Number(b), 16);
        view.setBigInt64(0, BigInt(Math.floor(now / 1000)), true);
        view.setBigInt64(8, BigInt(Math.floor((now % 1000) * 1000000)), true);
        return 0n;
      }
      case 60: case 231: throw new ExitSignal(Number(BigInt.asIntN(32, a)));
      case 257: return this.open(b, c);
      case 262: return this.stat(b, c);
      case 318: return this.random(a, b);
      default: return -ENOSYS;
    }
  }
  imports() {
    return { wood: { syscall: (n, a, b, c, d) => this.syscall(n, a, b, c, d) } };
  }
  flush() {
    for (let stream = 0; stream < 2; stream += 1) {
      const rest = this.decoder[stream].decode();
      if (rest) this.onOutput(stream === 0 ? 'stdout' : 'stderr', rest);
    }
  }
}

// Starts a linked program's arguments in its own memory: the runtime keeps the pointers, and
// reads the environment from just after them, as on Linux. Here it is empty.
function placeArguments(memory, exports, list) {
  const text = list.map((value) => encoder.encode(`${value}\0`));
  const total = text.reduce((sum, item) => sum + item.length, 0) + 8 * (list.length + 2);
  const block = Number(exports.wd_platform_alloc(BigInt(total)));
  if (block === 0) throw new LimitSignal('memory');
  const view = new DataView(memory.buffer);
  let at = block + 8 * (list.length + 2);
  text.forEach((item, index) => {
    view.setBigUint64(block + 8 * index, BigInt(at), true);
    new Uint8Array(memory.buffer, at, item.length).set(item);
    at += item.length;
  });
  view.setBigUint64(block + 8 * list.length, 0n, true);
  view.setBigUint64(block + 8 * (list.length + 1), 0n, true);
  exports.wd_m_args_init(BigInt(list.length), BigInt(block));
}

function classify(error, sandbox) {
  sandbox.flush();
  if (error instanceof ExitSignal) return { status: error.code };
  if (error instanceof LimitSignal) return { status: 137, limit: error.kind };
  // Chromium raises a RangeError when the stack runs out, and Firefox an InternalError.
  if (error instanceof RangeError || error?.name === 'InternalError') return { status: 134, limit: 'stack' };
  if (error instanceof WebAssembly.RuntimeError) {
    const stack = /call stack|stack overflow/i.test(error.message);
    return { status: 134, limit: stack ? 'stack' : 'trap', detail: error.message, trace: error.stack };
  }
  throw error;
}

// A module linked with its own primitive runtime, such as the compiler.
export async function runLinked(module, sandbox, list, { maximumPages = 4096 } = {}) {
  const memory = new WebAssembly.Memory({ initial: 256, maximum: maximumPages });
  sandbox.memory = memory;
  const instance = await WebAssembly.instantiate(module, { ...sandbox.imports(), env: { memory } });
  growTo(memory, Number(instance.exports.__heap_base.value));
  try {
    placeArguments(memory, instance.exports, list);
    const status = instance.exports.wd_main();
    sandbox.flush();
    return { status };
  } catch (error) {
    return classify(error, sandbox);
  }
}

function growTo(memory, bytes) {
  const pages = Math.ceil(bytes / 65536) - memory.buffer.byteLength / 65536;
  if (pages > 0) memory.grow(pages);
}

// What a program needs from the runtime before it is compiled against it.
export async function runtimeLayout(runtime) {
  const probe = new Sandbox();
  const memory = new WebAssembly.Memory({ initial: 256, maximum: 4096 });
  probe.memory = memory;
  const instance = await WebAssembly.instantiate(runtime, { ...probe.imports(), env: { memory } });
  return { memoryBase: Number(instance.exports.__heap_base.value), tableBase: instance.exports.__indirect_function_table.length };
}

function programLayout(program) {
  const [note] = WebAssembly.Module.customSections(program, 'wood.layout');
  const view = new DataView(note);
  return { memoryEnd: Number(view.getBigUint64(0, true)), tableEnd: Number(view.getBigUint64(8, true)) };
}

// Runs a compiled program against a fresh runtime: new memory, new table, new everything.
export async function runProgram(runtime, program, sandbox, { maximumPages = 4096, list = ['program'] } = {}) {
  const memory = new WebAssembly.Memory({ initial: 256, maximum: maximumPages });
  sandbox.memory = memory;
  const base = await WebAssembly.instantiate(runtime, { ...sandbox.imports(), env: { memory } });
  const layout = programLayout(program);
  growTo(memory, layout.memoryEnd);
  const table = base.exports.__indirect_function_table;
  if (table.length < layout.tableEnd) table.grow(layout.tableEnd - table.length);
  const provided = { memory, table };
  for (const { module: from, name, kind } of WebAssembly.Module.imports(program)) {
    if (from === 'rt' && kind === 'function') {
      if (!(name in base.exports)) throw new Error(`the runtime does not provide ${name}`);
      provided[name] = base.exports[name];
    }
  }
  const instance = await WebAssembly.instantiate(program, { rt: provided });
  try {
    placeArguments(memory, base.exports, list);
    const status = instance.exports.wd_main();
    sandbox.flush();
    return { status };
  } catch (error) {
    return classify(error, sandbox);
  }
}

// The real compiler, in its own sandbox, turning source into a program module.
export async function compileSource(compiler, assets, source, layout, { entry = '/work/main.wd' } = {}) {
  const files = new Files({ '/assets/stdlib.json': assets.stdlib, '/assets/native/wasm64-browser.ll': assets.platform, '/assets/native/core.ll': assets.core, [entry]: source }, 64 * 1024 * 1024);
  files.directories.add('/assets');
  files.directories.add('/assets/native');
  let diagnostics = '';
  const sandbox = new Sandbox({ files, onOutput: (stream, text) => { if (stream === 'stderr') diagnostics += text; } });
  const output = '/work/program.wasm';
  const list = ['wood-playground', 'wasm', '/assets/stdlib.json', '/assets/native', entry, output, String(layout.memoryBase), String(layout.tableBase)];
  const result = await runLinked(compiler, sandbox, list, { maximumPages: 8192 });
  if (result.status !== 0) return { ok: false, status: result.status, diagnostics, limit: result.limit, detail: result.detail };
  const bytes = files.files.get(output);
  return { ok: true, bytes, diagnostics };
}

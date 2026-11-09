// A minimal Language Server Protocol client over real pipes. Uses only Node's standard library.
import { spawn } from 'node:child_process';
import { pathToFileURL } from 'node:url';

export const uri = (file) => pathToFileURL(file).href;

export class Client {
    constructor(wood, cwd) {
        this.process = spawn(wood, ['lsp', '--stdio'], { cwd, stdio: ['pipe', 'pipe', 'pipe'] });
        this.buffer = Buffer.alloc(0);
        this.next = 1;
        this.pending = new Map();
        this.diagnostics = new Map();
        this.notifications = [];
        this.stderr = '';
        this.exit = new Promise((resolve) => this.process.on('exit', (code) => resolve(code)));
        this.process.stderr.on('data', (data) => { this.stderr += data; });
        this.process.stdout.on('data', (data) => this.receive(data));
        this.process.stdin.on('error', () => {});
    }

    receive(data) {
        this.buffer = Buffer.concat([this.buffer, data]);
        for (;;) {
            const end = this.buffer.indexOf('\r\n\r\n');
            if (end < 0) return;
            const match = /Content-Length: (\d+)/i.exec(this.buffer.subarray(0, end).toString());
            if (!match) throw new Error('response has no Content-Length');
            const total = end + 4 + Number(match[1]);
            if (this.buffer.length < total) return;
            const message = JSON.parse(this.buffer.subarray(end + 4, total).toString());
            this.buffer = this.buffer.subarray(total);
            if (message.id !== undefined && this.pending.has(message.id)) {
                const { resolve } = this.pending.get(message.id);
                this.pending.delete(message.id);
                resolve(message);
            } else if (message.method === 'textDocument/publishDiagnostics') {
                this.diagnostics.set(message.params.uri, message.params.diagnostics);
                this.notifications.push(message);
            } else {
                this.notifications.push(message);
            }
        }
    }

    write(message) {
        const body = Buffer.from(JSON.stringify({ jsonrpc: '2.0', ...message }));
        this.process.stdin.write(Buffer.concat([Buffer.from(`Content-Length: ${body.length}\r\n\r\n`), body]));
    }

    // The whole response, so tests can look at errors.
    raw(method, params, timeout = 60000) {
        const id = this.next++;
        return new Promise((resolve, reject) => {
            const timer = setTimeout(() => reject(new Error(`${method} timed out`)), timeout);
            this.pending.set(id, { resolve: (message) => { clearTimeout(timer); resolve(message); } });
            this.write({ id, method, params });
        });
    }

    async request(method, params) {
        const message = await this.raw(method, params);
        if (message.error) throw new Error(`${method}: ${message.error.message}`);
        return message.result;
    }

    notify(method, params) {
        this.write({ method, params });
    }

    async initialize(root) {
        const result = await this.request('initialize', {
            processId: process.pid,
            rootUri: root ? uri(root) : null,
            workspaceFolders: root ? [{ uri: uri(root), name: 'test' }] : null,
            capabilities: {},
        });
        this.notify('initialized', {});
        return result;
    }

    open(file, text, version = 1) {
        this.notify('textDocument/didOpen', { textDocument: { uri: uri(file), languageId: 'wood', version, text } });
    }

    change(file, version, changes) {
        this.notify('textDocument/didChange', { textDocument: { uri: uri(file), version }, contentChanges: changes });
    }

    flush() {
        return this.request('wood/flush', {});
    }

    at(file, line, character) {
        return { textDocument: { uri: uri(file) }, position: { line, character } };
    }

    async stop() {
        await this.request('shutdown', null);
        this.notify('exit', null);
        return this.exit;
    }

    kill() {
        this.process.kill();
    }
}

// The line and character of a marker, so tests stay readable when fixtures change.
export function find(text, needle, offset = 0, nth = 0) {
    let index = -1;
    for (let i = 0; i <= nth; i++) {
        index = text.indexOf(needle, index + 1);
        if (index < 0) throw new Error(`fixture has no ${needle}`);
    }
    index += offset;
    const before = text.slice(0, index).split('\n');
    return { line: before.length - 1, character: before[before.length - 1].length };
}

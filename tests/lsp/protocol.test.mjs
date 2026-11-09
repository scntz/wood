// Protocol and editor-workflow tests for wood lsp, against a real multi-file project.
// Usage: WOOD=path/to/wood node --test tests/lsp/protocol.test.mjs
import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, mkdirSync, writeFileSync, rmSync, readFileSync, realpathSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { Client, find, uri } from './client.mjs';

const WOOD = process.env.WOOD;
if (!WOOD) throw new Error('Set WOOD to the wood executable');

const SHAPES = `# Geometry for the demo.
record Point:
    x: int
    y: int

    fn moved(self, dx: int) -> Point:
        # Returns a copy, further along x.
        return Point(self.x + dx, self.y)

# The distance from the origin along both axes.
fn manhattan(point: Point, scale: int = 1) -> int:
    return (point.x + point.y) * scale

fn _hidden() -> int:
    return 1
`;

const MAIN = `import shapes.geometry as geo
import std.path as path

fn describe(point: geo.Point, label: str) -> str:
    total = geo.manhattan(point, scale=2)
    moved = point.moved(3)
    for item in [1, 2]:
        total += item
    return label + str(total + moved.x)

origin = geo.Point(1, 2)
print(describe(origin, "a"))
print(path.name("x/y"))
`;

const BROKEN = `fn add(a: int, b: int) -> int:
    return a + "text"

fn other() -> int:
    missing_name(1)
    return 1

x: int = "wrong"
print(undefined_thing)
`;

let root, shapes, main, client;
const messages = (file) => (client.diagnostics.get(uri(file)) ?? []).map((item) => item.message);

before(async () => {
    root = realpathSync(mkdtempSync(join(tmpdir(), 'wood-lsp-')));
    mkdirSync(join(root, 'src', 'shapes'), { recursive: true });
    mkdirSync(join(root, 'tests'));
    writeFileSync(join(root, 'wood.json'), JSON.stringify({ format: 1, name: 'demo', version: '0.1.0', source: 'src', entry: 'main.wd', tests: 'tests', dependencies: {} }));
    shapes = join(root, 'src', 'shapes', 'geometry.wd');
    main = join(root, 'src', 'main.wd');
    writeFileSync(shapes, SHAPES);
    writeFileSync(main, MAIN);
    client = new Client(WOOD, root);
    await client.initialize(root);
});

after(async () => {
    const code = await client.stop();
    assert.equal(code, 0, 'clean shutdown exits with zero');
    assert.equal(client.stderr, '', `nothing was logged: ${client.stderr.slice(0, 1500)}`);
    rmSync(root, { recursive: true, force: true });
});

test('initialize advertises what is implemented', async () => {
    const other = new Client(WOOD, root);
    const { capabilities, serverInfo } = await other.initialize(null);
    assert.equal(serverInfo.name, 'wood');
    assert.equal(capabilities.textDocumentSync.change, 2, 'incremental sync');
    for (const name of ['hoverProvider', 'definitionProvider', 'referencesProvider', 'documentSymbolProvider', 'workspaceSymbolProvider']) {
        assert.equal(capabilities[name], true, name);
    }
    assert.ok(capabilities.completionProvider.triggerCharacters.includes('.'));
    assert.ok(capabilities.signatureHelpProvider);
    assert.ok(capabilities.renameProvider.prepareProvider);
    assert.equal(capabilities.documentFormattingProvider, true);
    assert.ok(capabilities.semanticTokensProvider.legend.tokenTypes.includes('function'));
    assert.equal(await other.stop(), 0);
});

test('requests before initialize are refused, and the server survives bad input', async () => {
    const other = new Client(WOOD, root);
    const early = await other.raw('textDocument/hover', other.at(main, 0, 0));
    assert.equal(early.error.code, -32002);
    await other.initialize(null);
    const body = Buffer.from('{not json');
    other.process.stdin.write(Buffer.concat([Buffer.from(`Content-Length: ${body.length}\r\n\r\n`), body]));
    const unknown = await other.raw('no/such/method', {});
    assert.equal(unknown.error.code, -32601);
    assert.equal((await other.raw('textDocument/hover', {})).error.code, -32603, 'missing params are an error, not a crash');
    assert.equal(await other.stop(), 0);
});

test('exit without shutdown reports failure, as the protocol requires', async () => {
    const other = new Client(WOOD, root);
    await other.initialize(null);
    other.notify('exit', null);
    assert.equal(await other.exit, 1);
});

test('end of input ends the server rather than spinning', async () => {
    const other = new Client(WOOD, root);
    await other.initialize(null);
    other.process.stdin.end();
    assert.equal(await other.exit, 1);
});

test('a correct project has no diagnostics', async () => {
    client.open(main, MAIN);
    await client.flush();
    assert.deepEqual(messages(main), []);
});

test('every error in a file is reported, without follow-on noise', async () => {
    const file = join(root, 'src', 'broken.wd');
    writeFileSync(file, BROKEN);
    client.open(file, BROKEN);
    await client.flush();
    const found = client.diagnostics.get(uri(file));
    const lines = found.map((item) => item.range.start.line).sort((a, b) => a - b);
    assert.deepEqual(lines, [1, 4, 7, 8], JSON.stringify(found));
    const type = found.find((item) => item.range.start.line === 1);
    assert.equal(type.code, 'E3002');
    assert.equal(type.source, 'wood');
    assert.equal(type.severity, 1);
    assert.ok(found.find((item) => item.range.start.line === 8).message.includes('undefined_thing'));
    client.notify('textDocument/didClose', { textDocument: { uri: uri(file) } });
    await client.flush();
    assert.deepEqual(messages(file), [], 'a closed lone file stops reporting');
    rmSync(file);
});

test('a variable whose assignment failed does not produce a second error', async () => {
    const file = join(root, 'src', 'cascade.wd');
    const text = 'value = undefined_call()\nprint(value)\nother = value\nprint(other)\n';
    writeFileSync(file, text);
    client.open(file, text);
    await client.flush();
    assert.equal(messages(file).length, 1, messages(file).join('|'));
    client.notify('textDocument/didClose', { textDocument: { uri: uri(file) } });
    await client.flush();
    rmSync(file);
});

test('formatting returns the whole document as wood fmt lays it out', async () => {
    const file = join(root, 'src', 'untidy.wd');
    const options = { tabSize: 4, insertSpaces: true };
    client.open(file, 'fn  twice(x:int)->int:\n  return x+x\n');
    const edits = await client.request('textDocument/formatting', { textDocument: { uri: uri(file) }, options });
    assert.equal(edits.length, 1);
    assert.deepEqual(edits[0].range.start, { line: 0, character: 0 });
    assert.equal(edits[0].newText, 'fn twice(x: int) -> int:\n    return x + x\n');
    client.notify('textDocument/didChange', { textDocument: { uri: uri(file), version: 2 }, contentChanges: [{ text: edits[0].newText }] });
    assert.deepEqual(await client.request('textDocument/formatting', { textDocument: { uri: uri(file) }, options }), [], 'already formatted');
    client.notify('textDocument/didChange', { textDocument: { uri: uri(file), version: 3 }, contentChanges: [{ text: 'x = (1,\n' }] });
    assert.deepEqual(await client.request('textDocument/formatting', { textDocument: { uri: uri(file) }, options }), [], 'a syntax error gets no edit');
    client.notify('textDocument/didClose', { textDocument: { uri: uri(file) } });
    await client.flush();
});

test('a document that exists only in the editor still works', async () => {
    const file = join(root, 'src', 'never_saved.wd');
    const text = 'fn twice(x: int) -> int:\n    return x + x\n\nprint(twice("no"))\n';
    client.open(file, text);
    await client.flush();
    assert.equal(messages(file).length, 1, messages(file).join('|'));
    const hover = await client.request('textDocument/hover', client.at(file, 3, 7));
    assert.ok(hover.contents.value.includes('fn twice(x: int) -> int'));
    client.notify('textDocument/didClose', { textDocument: { uri: uri(file) } });
    await client.flush();
    assert.deepEqual(messages(file), []);
});

test('diagnostics still arrive when a request gets to the new text first', async () => {
    // What an editor does: a change, followed at once by requests such as highlighting.
    const file = join(root, 'src', 'requests_first.wd');
    const good = 'value = 1\nprint(value)\n';
    writeFileSync(file, good);
    client.open(file, good);
    await client.flush();
    assert.deepEqual(messages(file), []);
    client.change(file, 2, [{ text: 'value = 1\nprint(missing_name)\n' }]);
    await client.request('textDocument/semanticTokens/full', { textDocument: { uri: uri(file) } });
    await client.flush();
    assert.equal(messages(file).length, 1, 'the error is reported');
    client.change(file, 3, [{ text: good }]);
    await client.request('textDocument/semanticTokens/full', { textDocument: { uri: uri(file) } });
    await client.request('textDocument/hover', client.at(file, 1, 7));
    await client.flush();
    assert.deepEqual(messages(file), [], 'and the fix clears it');
    client.notify('textDocument/didClose', { textDocument: { uri: uri(file) } });
    await client.flush();
    rmSync(file);
});

test('a missing import is reported on the import, and only there', async () => {
    const file = join(root, 'src', 'imports.wd');
    const text = 'import nothing.here as gone\nprint(gone.call(1))\nprint(1 + "x")\n';
    writeFileSync(file, text);
    client.open(file, text);
    await client.flush();
    const found = client.diagnostics.get(uri(file));
    assert.deepEqual(found.map((item) => item.range.start.line).sort(), [0, 2], JSON.stringify(found));
    assert.equal(found.find((item) => item.range.start.line === 0).code, 'E3101');
    client.notify('textDocument/didClose', { textDocument: { uri: uri(file) } });
    await client.flush();
    rmSync(file);
});

test('syntax errors are reported, and the rest of the file still works', async () => {
    const file = join(root, 'src', 'syntax.wd');
    const text = 'fn good(a: int) -> int:\n    return a\n\nfn bad() -> int:\n    y = (1 +\n    return 1\n\nvalue = good(2)\nprint(value)\nz = \n';
    writeFileSync(file, text);
    client.open(file, text);
    await client.flush();
    assert.ok(messages(file).length >= 1);
    const hover = await client.request('textDocument/hover', client.at(file, 7, 9));
    assert.ok(hover.contents.value.includes('fn good(a: int) -> int'), JSON.stringify(hover));
    client.notify('textDocument/didClose', { textDocument: { uri: uri(file) } });
    await client.flush();
    rmSync(file);
});

test('hover shows signatures, types and documentation', async () => {
    const function_hover = await client.request('textDocument/hover', client.at(main, 4, 18));
    assert.ok(function_hover.contents.value.includes('fn manhattan(point: Point, scale: int = ...) -> int'), JSON.stringify(function_hover));
    assert.ok(function_hover.contents.value.includes('The distance from the origin'));
    const local = find(MAIN, 'total = geo');
    const local_hover = await client.request('textDocument/hover', client.at(main, local.line, local.character));
    assert.ok(local_hover.contents.value.includes('total: int'), JSON.stringify(local_hover));
    const parameter = find(MAIN, 'label + str');
    const parameter_hover = await client.request('textDocument/hover', client.at(main, parameter.line, parameter.character));
    assert.ok(parameter_hover.contents.value.includes('parameter label: str'));
    const loop = find(MAIN, 'total += item', 9);
    const loop_hover = await client.request('textDocument/hover', client.at(main, loop.line, loop.character));
    assert.ok(loop_hover.contents.value.includes('loop item: int'), JSON.stringify(loop_hover));
    const std = find(MAIN, 'name("x');
    const std_hover = await client.request('textDocument/hover', client.at(main, std.line, std.character));
    assert.ok(std_hover.contents.value.includes('fn name(path: str) -> str') || std_hover.contents.value.includes('fn name('), JSON.stringify(std_hover));
    const builtin = find(MAIN, 'print(describe');
    const builtin_hover = await client.request('textDocument/hover', client.at(main, builtin.line, builtin.character));
    assert.ok(builtin_hover.contents.value.includes('print'));
    assert.equal(await client.request('textDocument/hover', client.at(main, 3, 0)), null, 'a keyword has no hover');
});

test('method documentation comes from the body, and methods hide self', async () => {
    const place = find(MAIN, 'moved(3)');
    const hover = await client.request('textDocument/hover', client.at(main, place.line, place.character));
    assert.ok(hover.contents.value.includes('fn moved(dx: int) -> Point'), JSON.stringify(hover));
    assert.ok(hover.contents.value.includes('Returns a copy'));
});

test('go to definition crosses files', async () => {
    const place = find(MAIN, 'manhattan');
    const location = await client.request('textDocument/definition', client.at(main, place.line, place.character));
    assert.equal(location.uri, uri(shapes));
    assert.deepEqual(location.range.start, find(SHAPES, 'manhattan'));
    const field = find(MAIN, 'moved.x', 6);
    const field_location = await client.request('textDocument/definition', client.at(main, field.line, field.character));
    assert.equal(field_location.uri, uri(shapes));
    assert.deepEqual(field_location.range.start, find(SHAPES, 'x: int'));
    const record = find(MAIN, 'Point(1');
    const record_location = await client.request('textDocument/definition', client.at(main, record.line, record.character));
    assert.deepEqual(record_location.range.start, find(SHAPES, 'Point:'));
    const local = find(MAIN, 'total + moved');
    const local_location = await client.request('textDocument/definition', client.at(main, local.line, local.character));
    assert.deepEqual(local_location.range.start, find(MAIN, 'total = geo'));
    const argument = find(MAIN, 'scale=2');
    const argument_location = await client.request('textDocument/definition', client.at(main, argument.line, argument.character));
    assert.deepEqual(argument_location.range.start, find(SHAPES, 'scale: int'), 'a keyword argument goes to the parameter');
    const module = find(MAIN, 'shapes.geometry');
    const module_location = await client.request('textDocument/definition', client.at(main, module.line, module.character));
    assert.equal(module_location.uri, uri(shapes));
});

test('go to definition reaches the embedded standard library', async () => {
    const place = find(MAIN, 'name("x');
    const location = await client.request('textDocument/definition', client.at(main, place.line, place.character));
    assert.ok(location.uri.startsWith('file:'), location.uri);
    assert.ok(location.uri.endsWith('std_path.wd'), location.uri);
    const text = readFileSync(new URL(location.uri), 'utf8');
    assert.ok(text.split('\n')[location.range.start.line].includes('name'));
});

test('find references crosses files and tells symbols apart', async () => {
    const place = find(SHAPES, 'manhattan');
    client.open(shapes, SHAPES);
    const locations = await client.request('textDocument/references', { ...client.at(shapes, place.line, place.character), context: { includeDeclaration: true } });
    assert.equal(locations.length, 2, JSON.stringify(locations));
    assert.deepEqual(new Set(locations.map((item) => item.uri)), new Set([uri(shapes), uri(main)]));
    const without = await client.request('textDocument/references', { ...client.at(shapes, place.line, place.character), context: { includeDeclaration: false } });
    assert.equal(without.length, 1);
    const local = find(MAIN, 'total = geo');
    const local_uses = await client.request('textDocument/references', { ...client.at(main, local.line, local.character), context: { includeDeclaration: true } });
    assert.equal(local_uses.length, 3, 'declaration, += and the use in return');
    const field = find(SHAPES, 'x: int');
    const field_uses = await client.request('textDocument/references', { ...client.at(shapes, field.line, field.character), context: { includeDeclaration: true } });
    assert.ok(field_uses.length >= 4, JSON.stringify(field_uses));
    client.notify('textDocument/didClose', { textDocument: { uri: uri(shapes) } });
});

test('document symbols form an outline', async () => {
    const symbols = await client.request('textDocument/documentSymbol', { textDocument: { uri: uri(shapes) } });
    assert.deepEqual(symbols.map((item) => item.name), ['Point', 'manhattan', '_hidden']);
    const point = symbols[0];
    assert.deepEqual(point.children.map((item) => item.name), ['x', 'y', 'moved']);
    assert.equal(point.range.start.line, 1);
    assert.equal(point.range.end.line, 7);
    assert.deepEqual(point.selectionRange.start, find(SHAPES, 'Point:'));
});

test('workspace symbols search the project, and fuzzy matching works', async () => {
    const found = await client.request('workspace/symbol', { query: 'mhtn' });
    assert.ok(found.some((item) => item.name === 'manhattan' && item.location.uri === uri(shapes)), JSON.stringify(found));
    const method = await client.request('workspace/symbol', { query: 'moved' });
    assert.equal(method.find((item) => item.name === 'moved').containerName, 'Point');
});

const labels = (result) => result.items.map((item) => item.label);

test('completion offers locals, functions, modules, builtins and keywords in scope', async () => {
    const place = find(MAIN, '    return label');
    const result = await client.request('textDocument/completion', client.at(main, place.line, 4));
    for (const name of ['label', 'point', 'total', 'moved', 'describe', 'geo', 'path', 'print', 'return']) {
        assert.ok(labels(result).includes(name), name);
    }
    assert.ok(!labels(result).includes('item'), 'a loop variable is out of scope after its loop');
    const top = await client.request('textDocument/completion', client.at(main, 11, 0));
    assert.ok(!labels(top).includes('label'), 'parameters are not visible at top level');
});

async function completionAfter(prefix) {
    const text = MAIN.replace('    return label', `    ${prefix}\n    return label`);
    client.change(main, 2, [{ text }]);
    const place = find(text, prefix, prefix.length);
    const result = await client.request('textDocument/completion', client.at(main, place.line, place.character));
    client.change(main, 3, [{ text: MAIN }]);
    return result;
}

test('completion works on a line that does not parse yet', async () => {
    const module = await completionAfter('geo.');
    assert.ok(labels(module).includes('manhattan') && labels(module).includes('Point'), JSON.stringify(labels(module)));
    assert.ok(!labels(module).includes('_hidden'), 'private names are not exported');
    const fields = await completionAfter('point.');
    assert.deepEqual(new Set(labels(fields)), new Set(['x', 'y', 'moved']));
    const chained = await completionAfter('point.moved(1).');
    assert.deepEqual(new Set(labels(chained)), new Set(['x', 'y', 'moved']));
    const std = await completionAfter('path.');
    assert.ok(labels(std).includes('name'));
});

test('completion stays quiet in strings and comments', async () => {
    const text = 'print("a.b.\n# geo.';
    const file = join(root, 'src', 'quiet.wd');
    writeFileSync(file, text);
    client.open(file, text);
    assert.deepEqual(labels(await client.request('textDocument/completion', client.at(file, 0, 11))), []);
    assert.deepEqual(labels(await client.request('textDocument/completion', client.at(file, 1, 6))), []);
    client.notify('textDocument/didClose', { textDocument: { uri: uri(file) } });
    rmSync(file);
});

test('import completion lists modules', async () => {
    const file = join(root, 'src', 'importing.wd');
    writeFileSync(file, 'import std.');
    client.open(file, 'import std.');
    const result = await client.request('textDocument/completion', client.at(file, 0, 11));
    assert.ok(labels(result).includes('std.path'), JSON.stringify(labels(result).slice(0, 5)));
    client.notify('textDocument/didClose', { textDocument: { uri: uri(file) } });
    rmSync(file);
});

test('signature help follows the argument being typed', async () => {
    const text = MAIN.replace('    return label', '    geo.manhattan(point, \n    return label');
    client.change(main, 4, [{ text }]);
    const place = find(text, 'point, \n', 7);
    const help = await client.request('textDocument/signatureHelp', client.at(main, place.line, place.character));
    assert.equal(help.signatures[0].label, 'fn manhattan(point: Point, scale: int = ...) -> int');
    assert.equal(help.activeParameter, 1);
    assert.equal(help.signatures[0].parameters.length, 2);
    client.change(main, 5, [{ text: MAIN }]);
    const method = MAIN.indexOf('moved(3') + 6;
    const line = MAIN.slice(0, method).split('\n').length - 1;
    const character = method - MAIN.lastIndexOf('\n', method) - 1;
    const method_help = await client.request('textDocument/signatureHelp', client.at(main, line, character));
    assert.equal(method_help.signatures[0].label, 'fn moved(dx: int) -> Point', 'self is hidden');
    assert.equal(method_help.activeParameter, 0);
    const builtin = find(MAIN, 'print(describe', 6);
    const builtin_help = await client.request('textDocument/signatureHelp', client.at(main, builtin.line, builtin.character));
    assert.ok(builtin_help.signatures[0].label.includes('print'));
    const none = find(MAIN, 'origin = geo');
    assert.equal(await client.request('textDocument/signatureHelp', client.at(main, none.line, 0)), null);
});

test('incremental edits apply exactly, including non-ASCII text', async () => {
    const file = join(root, 'src', 'unicode.wd');
    const text = 'label = "héllo \u{1F600}"; value = 1\nprint(value)\n';
    const safe = 'label = "héllo \u{1F600}"\nvalue = 1\nprint(value)\n';
    writeFileSync(file, safe);
    assert.ok(text);
    client.open(file, safe);
    await client.flush();
    assert.deepEqual(messages(file), []);
    // Rename the variable through an edit placed after non-ASCII text on a line.
    const unicode = 'text = "\u{1F600}" + undefined_after_emoji\n';
    client.change(file, 2, [{ text: unicode }]);
    await client.flush();
    const found = client.diagnostics.get(uri(file))[0];
    assert.equal(found.range.start.character, 'text = "\u{1F600}" + '.length, 'UTF-16 column after an emoji');
    client.change(file, 3, [{ range: { start: { line: 0, character: 'text = "\u{1F600}" + '.length }, end: { line: 0, character: unicode.length - 1 } }, text: '"ok"' }]);
    await client.flush();
    assert.deepEqual(messages(file), [], 'the edit landed where UTF-16 says');
    client.change(file, 4, [
        { range: { start: { line: 1, character: 0 }, end: { line: 1, character: 0 } }, text: 'a = 1\n' },
        { range: { start: { line: 2, character: 0 }, end: { line: 2, character: 0 } }, text: 'b = a + c_missing\n' },
    ]);
    await client.flush();
    const after = client.diagnostics.get(uri(file));
    assert.equal(after.length, 1);
    assert.equal(after[0].range.start.line, 2);
    client.notify('textDocument/didClose', { textDocument: { uri: uri(file) } });
    await client.flush();
    rmSync(file);
});

test('semantic tokens classify names and decode to the right places', async () => {
    const { data } = await client.request('textDocument/semanticTokens/full', { textDocument: { uri: uri(main) } });
    assert.equal(data.length % 5, 0);
    const legend = ['namespace', 'type', 'function', 'method', 'parameter', 'variable', 'property'];
    const lines = MAIN.split('\n');
    const decoded = [];
    let line = 0, column = 0;
    for (let i = 0; i < data.length; i += 5) {
        line += data[i];
        column = data[i] === 0 ? column + data[i + 1] : data[i + 1];
        decoded.push({ text: lines[line].slice(column, column + data[i + 2]), type: legend[data[i + 3]], modifiers: data[i + 4] });
    }
    const type_of = (text) => decoded.find((item) => item.text === text)?.type;
    assert.equal(type_of('geo'), 'namespace');
    assert.equal(type_of('describe'), 'function');
    assert.equal(type_of('label'), 'parameter');
    assert.equal(type_of('total'), 'variable');
    assert.equal(type_of('moved'), 'variable');
    assert.equal(type_of('Point'), 'type');
    assert.equal(type_of('manhattan'), 'function');
    assert.equal(decoded.find((item) => item.text === 'describe').modifiers, 1, 'the first use is its declaration');
    assert.equal(decoded.filter((item) => item.text === 'describe')[1].modifiers, 0);
});

test('rename updates every file, and keyword arguments too', async () => {
    client.open(shapes, SHAPES);
    const place = find(SHAPES, 'scale: int');
    const prepared = await client.request('textDocument/prepareRename', client.at(shapes, place.line, place.character));
    assert.deepEqual(prepared.start, place);
    const edit = await client.request('textDocument/rename', { ...client.at(shapes, place.line, place.character), newName: 'factor' });
    assert.equal(edit.changes[uri(shapes)].length, 2, 'declaration and use');
    assert.equal(edit.changes[uri(main)].length, 1, 'keyword argument');
    const local = find(MAIN, 'total = geo');
    const local_edit = await client.request('textDocument/rename', { ...client.at(main, local.line, local.character), newName: 'sum_total' });
    assert.equal(local_edit.changes[uri(main)].length, 3);
    const manhattan = find(SHAPES, 'manhattan');
    const function_edit = await client.request('textDocument/rename', { ...client.at(shapes, manhattan.line, manhattan.character), newName: 'taxicab' });
    assert.equal(Object.keys(function_edit.changes).length, 2);
    client.notify('textDocument/didClose', { textDocument: { uri: uri(shapes) } });
});

test('rename refuses what would not be safe', async () => {
    const refuse = async (file, line, character, newName) => {
        const response = await client.raw('textDocument/rename', { ...client.at(file, line, character), newName });
        assert.ok(response.error, JSON.stringify(response));
        return response.error.message;
    };
    const local = find(MAIN, 'total = geo');
    assert.match(await refuse(main, local.line, local.character, 'for'), /not a valid name/);
    assert.match(await refuse(main, local.line, local.character, '1abc'), /not a valid name/);
    assert.match(await refuse(main, local.line, local.character, 'label'), /cause an error|would/, 'a name collision');
    const builtin = find(MAIN, 'print(describe');
    assert.match(await refuse(main, builtin.line, builtin.character, 'show'), /built in/);
    const std = find(MAIN, 'name("x');
    assert.match(await refuse(main, std.line, std.character, 'label'), /standard library/);
    assert.ok((await client.raw('textDocument/prepareRename', client.at(main, 3, 0))).error, 'a keyword cannot be renamed');
});

test('an unsaved edit in one file is seen by another', async () => {
    client.open(shapes, SHAPES);
    client.change(shapes, 2, [{ text: SHAPES.replace('fn manhattan', 'fn renamed_function') }]);
    await client.flush();
    assert.ok(messages(main).some((message) => message.includes('manhattan')), messages(main).join('|'));
    client.change(shapes, 3, [{ text: SHAPES }]);
    await client.flush();
    assert.deepEqual(messages(main), []);
    client.notify('textDocument/didClose', { textDocument: { uri: uri(shapes) } });
    await client.flush();
});

test('project files get diagnostics without being opened', async () => {
    const other = new Client(WOOD, root);
    const closed = join(root, 'src', 'closed.wd');
    writeFileSync(closed, 'fn broken() -> int:\n    return "text"\n');
    await other.initialize(root);
    await other.flush();
    assert.equal(other.diagnostics.get(uri(closed))?.length, 1, 'found in the background');
    assert.equal(other.diagnostics.has(uri(main)), false, 'a clean file publishes nothing');
    writeFileSync(closed, 'fn broken() -> int:\n    return 1\n');
    other.notify('workspace/didChangeWatchedFiles', { changes: [{ uri: uri(closed), type: 2 }] });
    await other.flush();
    assert.deepEqual(other.diagnostics.get(uri(closed)), [], 'fixed on disk, cleared in the editor');
    assert.equal(await other.stop(), 0);
    rmSync(closed);
});

test('typing a whole file one prefix at a time never breaks the server', async () => {
    const file = join(root, 'src', 'typing.wd');
    const text = readFileSync(join(import.meta.dirname, 'fixtures', 'typing.wd'), 'utf8');
    writeFileSync(file, '');
    client.open(file, '');
    let version = 1;
    let last = '';
    // One message per step on a real pipe, as an editor would send them.
    for (let end = 3; end <= text.length; end += 3) {
        version += 1;
        const added = text.slice(last.length, end);
        const lines = last.split('\n');
        client.change(file, version, [{ range: { start: { line: lines.length - 1, character: lines[lines.length - 1].length }, end: { line: lines.length - 1, character: lines[lines.length - 1].length } }, text: added }]);
        last = text.slice(0, end);
        const place = find(last, last[last.length - 1], 0, last.split(last[last.length - 1]).length - 2);
        for (const method of ['textDocument/hover', 'textDocument/completion', 'textDocument/signatureHelp', 'textDocument/definition']) {
            const response = await client.raw(method, client.at(file, place.line, place.character + 1));
            assert.equal(response.error, undefined, `${method} at ${end}: ${JSON.stringify(response.error)}`);
        }
    }
    await client.flush();
    assert.equal(client.stderr, '', `nothing failed internally: ${client.stderr.slice(0, 1500)}`);
    client.notify('textDocument/didClose', { textDocument: { uri: uri(file) } });
    await client.flush();
    rmSync(file);
});

test('the server does not grow without bound over many edits', { skip: process.platform !== 'linux' }, async () => {
    const other = new Client(WOOD, root);
    await other.initialize(null);
    other.open(main, MAIN);
    const sample = () => Number(/VmRSS:\s+(\d+)/.exec(readFileSync(`/proc/${other.process.pid}/status`, 'utf8'))[1]);
    const pass = async (start) => {
        for (let i = 0; i < 100; i++) {
            other.change(main, start + i, [{ text: `${MAIN}\n# ${i}\n` }]);
            await other.request('textDocument/hover', other.at(main, 4, 18));
        }
    };
    await pass(2);
    const first = sample();
    await pass(102);
    await pass(202);
    const second = sample();
    assert.ok(second < first * 1.25, `memory went from ${first} to ${second} KiB`);
    assert.equal(await other.stop(), 0);
});

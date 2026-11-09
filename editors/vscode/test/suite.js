// Runs inside VS Code. Everything goes through VS Code's own commands, so this is what a user gets.
'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vscode = require('vscode');

const results = [];
async function test(name, body) {
    try {
        await body();
        results.push(`PASS ${name}`);
    } catch (error) {
        results.push(`FAIL ${name}: ${error && error.stack ? error.stack : error}`);
    }
}

async function until(description, check, timeout = 30000) {
    const end = Date.now() + timeout;
    for (;;) {
        const value = await check();
        if (value) return value;
        if (Date.now() > end) throw new Error(`Timed out waiting for ${description}`);
        await new Promise((resolve) => setTimeout(resolve, 100));
    }
}

async function restore(editor, text) {
    const whole = editor.document.validateRange(new vscode.Range(0, 0, editor.document.lineCount, 0));
    assert.ok(await editor.edit((edit) => edit.replace(whole, text)), 'the edit applied');
    assert.equal(editor.document.getText(), text);
}

const position = (document, text, offset = 0) => document.positionAt(document.getText().indexOf(text) + offset);

async function run() {
    const project = process.env.WOOD_TEST_PROJECT;
    const main = vscode.Uri.file(path.join(project, 'src', 'main.wd'));
    const shapes = vscode.Uri.file(path.join(project, 'src', 'shapes.wd'));
    const broken = vscode.Uri.file(path.join(project, 'src', 'broken.wd'));
    let document;

    await test('the extension activates on a wood project and starts the server', async () => {
        const extension = await until('the extension', () => vscode.extensions.getExtension('scntz.wood'));
        const api = await extension.activate();
        assert.ok(api.findWood().file, 'found wood through wood.path');
        await until('the server', () => api.isRunning());
    });

    await test('.wd files are the wood language, with grammar and configuration', async () => {
        document = await vscode.workspace.openTextDocument(main);
        await vscode.window.showTextDocument(document);
        assert.equal(document.languageId, 'wood');
    });

    await test('a clean file has no problems, and a closed project file with errors does', async () => {
        const found = await until('diagnostics for the unopened file', () => {
            const items = vscode.languages.getDiagnostics(broken);
            return items.length > 0 ? items : undefined;
        });
        assert.equal(found[0].source, 'wood');
        assert.equal(found[0].code, 'E3002');
        assert.equal(found[0].severity, vscode.DiagnosticSeverity.Error);
        assert.deepEqual(vscode.languages.getDiagnostics(main), []);
    });

    await test('hover shows the signature', async () => {
        const hovers = await until('hover', async () => {
            const items = await vscode.commands.executeCommand('vscode.executeHoverProvider', main, position(document, 'area(', 1));
            return items.length > 0 ? items : undefined;
        });
        assert.ok(hovers[0].contents[0].value.includes('fn area(box: Box) -> int'), hovers[0].contents[0].value);
    });

    await test('go to definition opens the other file', async () => {
        const locations = await vscode.commands.executeCommand('vscode.executeDefinitionProvider', main, position(document, 'area(', 1));
        const location = locations[0];
        const target = location.targetUri ?? location.uri;
        assert.equal(target.fsPath.toLowerCase(), shapes.fsPath.toLowerCase());
    });

    await test('find references', async () => {
        const locations = await vscode.commands.executeCommand('vscode.executeReferenceProvider', main, position(document, 'area(', 1));
        assert.equal(locations.length, 2);
    });

    await test('IntelliSense lists module members, in real editing', async () => {
        const editor = await vscode.window.showTextDocument(document);
        const line = document.lineAt(document.lineCount - 1).range.end;
        const original = document.getText();
        await editor.edit((edit) => edit.insert(line, '\nshapes.'));
        const place = document.lineAt(document.lineCount - 1).range.end;
        const list = await vscode.commands.executeCommand('vscode.executeCompletionItemProvider', main, place, '.');
        const labels = list.items.map((item) => (typeof item.label === 'string' ? item.label : item.label.label));
        assert.ok(labels.includes('area') && labels.includes('Box'), labels.join(','));
        await restore(editor, original);
    });

    await test('a syntax error appears while typing, and clears when fixed', async () => {
        const editor = await vscode.window.showTextDocument(document);
        const line = document.lineAt(document.lineCount - 1).range.end;
        const original = document.getText();
        await editor.edit((edit) => edit.insert(line, '\nundefined_name_here()'));
        await until('the new problem', () => vscode.languages.getDiagnostics(main).some((item) => item.message.includes('undefined_name_here')));
        await restore(editor, original);
        await until('the problem to clear', () => vscode.languages.getDiagnostics(main).length === 0, 10000).catch((error) => {
            throw new Error(`${error.message}. Text: ${JSON.stringify(document.getText())} Problems: ${JSON.stringify(vscode.languages.getDiagnostics(main))}`);
        });
    });

    await test('signature help', async () => {
        const help = await vscode.commands.executeCommand('vscode.executeSignatureHelpProvider', main, position(document, 'Box(2', 4));
        assert.ok(help.signatures[0].label.includes('record Box'), help.signatures[0].label);
    });

    await test('outline and workspace symbols', async () => {
        const open = await vscode.workspace.openTextDocument(shapes);
        const symbols = await vscode.commands.executeCommand('vscode.executeDocumentSymbolProvider', shapes);
        assert.deepEqual(symbols.map((item) => item.name), ['Box', 'area']);
        assert.equal(symbols[0].children.length, 2);
        assert.ok(open);
        const found = await vscode.commands.executeCommand('vscode.executeWorkspaceSymbolProvider', 'area');
        assert.ok(found.some((item) => item.name === 'area'));
    });

    await test('rename edits both files', async () => {
        const edit = await vscode.commands.executeCommand('vscode.executeDocumentRenameProvider', main, position(document, 'area(', 1), 'surface');
        assert.equal(edit.size, 2);
    });

    await test('semantic tokens use the published legend', async () => {
        const legend = await vscode.commands.executeCommand('vscode.provideDocumentSemanticTokensLegend', main);
        assert.ok(legend.tokenTypes.includes('namespace'));
        const tokens = await vscode.commands.executeCommand('vscode.provideDocumentSemanticTokens', main);
        assert.ok(tokens.data.length > 0);
    });

    await test('the problem matcher parses what the compiler really prints', async () => {
        const extension = vscode.extensions.getExtension('scntz.wood');
        const matcher = extension.packageJSON.contributes.problemMatchers[0].pattern;
        const output = require('node:child_process').spawnSync(extension.exports.findWood().file, ['check', broken.fsPath], { encoding: 'utf8' });
        assert.equal(output.status, 1);
        const first = output.stderr.split(/\r?\n/)[0];
        const match = new RegExp(matcher.regexp).exec(first);
        assert.ok(match, first);
        assert.equal(match[matcher.line], '2');
        assert.equal(match[matcher.severity], 'error');
        assert.equal(match[matcher.code], 'E3002');
        assert.equal(path.resolve(match[matcher.file]).toLowerCase(), broken.fsPath.toLowerCase());
    });

    await test('tasks run the real CLI', async () => {
        const tasks = await vscode.tasks.fetchTasks({ type: 'wood' });
        assert.deepEqual(new Set(tasks.map((task) => task.definition.command)), new Set(['run', 'build', 'check', 'test']));
        const check = tasks.find((task) => task.definition.command === 'check');
        check.presentationOptions.reveal = vscode.TaskRevealKind.Never;
        const finished = new Promise((resolve) => {
            const listener = vscode.tasks.onDidEndTaskProcess((event) => {
                if (event.execution.task.definition.command === 'check') {
                    listener.dispose();
                    resolve(event.exitCode);
                }
            });
        });
        await vscode.tasks.executeTask(check);
        assert.equal(await finished, 0);
    });

    await test('the commands exist', async () => {
        const commands = await vscode.commands.getCommands(true);
        for (const name of ['wood.run', 'wood.build', 'wood.check', 'wood.test', 'wood.restartServer', 'wood.showOutput']) {
            assert.ok(commands.includes(name), name);
        }
    });

    await test('restarting the server works, and features come back', async () => {
        await vscode.commands.executeCommand('wood.restartServer');
        const api = vscode.extensions.getExtension('scntz.wood').exports;
        await until('the server', () => api.isRunning());
        const hovers = await until('hover after restart', async () => {
            const items = await vscode.commands.executeCommand('vscode.executeHoverProvider', main, position(document, 'area(', 1));
            return items.length > 0 ? items : undefined;
        });
        assert.ok(hovers.length > 0);
    });

    await test('a workspace setting cannot choose the program to run', async () => {
        const extension = vscode.extensions.getExtension('scntz.wood');
        const properties = extension.packageJSON.contributes.configuration.properties;
        assert.equal(properties['wood.path'].scope, 'machine');
        assert.ok(extension.exports.findWood().file);
    });

    await vscode.commands.executeCommand('workbench.action.closeAllEditors');
    const failed = results.filter((line) => line.startsWith('FAIL'));
    fs.writeFileSync(path.join(project, '..', 'results.txt'), results.join('\n'));
    for (const line of results) console.log(line);
    if (failed.length > 0) throw new Error(`${failed.length} of ${results.length} extension tests failed:\n${failed.join('\n')}`);
    console.log(`${results.length} extension tests passed`);
}

module.exports = { run };

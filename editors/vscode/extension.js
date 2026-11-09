// The wood extension. It starts `wood lsp` and adds run and build tasks. Everything about the
// language itself lives in the compiler, so this stays small and does no polling.
'use strict';

const vscode = require('vscode');
const path = require('node:path');
const fs = require('node:fs');
const os = require('node:os');
const { LanguageClient, TransportKind } = require('vscode-languageclient/node');

let client;
let output;
let statusItem;
const COMMANDS = ['run', 'build', 'check', 'test'];

function isExecutable(file) {
    try {
        if (!fs.statSync(file).isFile()) return false;
        if (process.platform !== 'win32') fs.accessSync(file, fs.constants.X_OK);
        return true;
    } catch {
        return false;
    }
}

function defaultLocations() {
    const home = os.homedir();
    if (process.platform === 'win32') {
        const local = process.env.LOCALAPPDATA ?? path.join(home, 'AppData', 'Local');
        return [path.join(local, 'Programs', 'wood', 'bin', 'wood.exe')];
    }
    return [path.join(home, '.local', 'bin', 'wood'), path.join(home, '.local', 'wood', 'bin', 'wood')];
}

// Only user settings, PATH and the standard install locations are searched. Nothing inside the
// workspace is ever run on its own say-so.
function findWood() {
    const configured = vscode.workspace.getConfiguration('wood').inspect('path')?.globalValue;
    if (configured) {
        const expanded = configured.startsWith('~') ? path.join(os.homedir(), configured.slice(1)) : configured;
        return isExecutable(expanded) ? { file: expanded } : { problem: `wood.path does not point to an executable: ${configured}` };
    }
    const name = process.platform === 'win32' ? 'wood.exe' : 'wood';
    for (const directory of (process.env.PATH ?? '').split(path.delimiter)) {
        if (directory && path.isAbsolute(directory) && isExecutable(path.join(directory, name))) {
            return { file: path.join(directory, name) };
        }
    }
    for (const candidate of defaultLocations()) {
        if (isExecutable(candidate)) return { file: candidate };
    }
    return { problem: 'Could not find the wood executable on PATH or in the default install locations.' };
}

function setStatus(text, tooltip, command) {
    statusItem.text = text;
    statusItem.tooltip = tooltip;
    statusItem.command = command;
    statusItem.show();
}

async function explainMissing(problem) {
    setStatus('$(warning) wood', problem, 'workbench.action.openSettings');
    const choice = await vscode.window.showWarningMessage(`wood: ${problem}`, 'Set Path', 'Install wood');
    if (choice === 'Set Path') vscode.commands.executeCommand('workbench.action.openSettings', 'wood.path');
    if (choice === 'Install wood') vscode.env.openExternal(vscode.Uri.parse('https://github.com/scntz/wood/releases'));
}

async function startServer() {
    await stopServer();
    if (!vscode.workspace.getConfiguration('wood').get('languageServer.enabled')) {
        statusItem.hide();
        return;
    }
    const wood = findWood();
    if (!wood.file) {
        explainMissing(wood.problem);
        return;
    }
    const command = { command: wood.file, args: ['lsp'], transport: TransportKind.stdio };
    client = new LanguageClient('wood', 'wood', { run: command, debug: command }, {
        documentSelector: [{ scheme: 'file', language: 'wood' }, { scheme: 'untitled', language: 'wood' }],
        outputChannel: output,
        synchronize: { fileEvents: vscode.workspace.createFileSystemWatcher('**/{*.wd,wood.json,wood.lock}') },
    });
    try {
        await client.start();
        setStatus('wood', `Language server: ${wood.file}`, 'wood.showOutput');
    } catch (error) {
        output.appendLine(String(error));
        setStatus('$(error) wood', 'The language server failed to start', 'wood.showOutput');
        // An older wood has no `lsp` command, and that is worth saying plainly.
        vscode.window.showErrorMessage(`wood: the language server could not start. This extension needs wood 0.5 or newer. Using: ${wood.file}`);
        client = undefined;
    }
}

async function stopServer() {
    const stopping = client;
    client = undefined;
    if (stopping) {
        try {
            await stopping.stop(5000);
        } catch (error) {
            output.appendLine(`Stopping the language server: ${error}`);
        }
    }
}

function folderFor(file) {
    return (file && vscode.workspace.getWorkspaceFolder(vscode.Uri.file(file))) ?? vscode.workspace.workspaceFolders?.[0];
}

function createTask(command, file, args = [], folder = folderFor(file)) {
    const wood = findWood();
    const definition = { type: 'wood', command, ...(file ? { file } : {}), ...(args.length ? { args } : {}) };
    const name = file ? `${command} ${path.basename(file)}` : command;
    // Task arguments are never passed through a shell, so a file name cannot run anything.
    const execution = new vscode.ProcessExecution(wood.file, [command, ...(file ? [file] : []), ...args], folder ? { cwd: folder.uri.fsPath } : {});
    const task = new vscode.Task(definition, folder ?? vscode.TaskScope.Workspace, name, 'wood', execution, ['$wood']);
    if (command === 'build') task.group = vscode.TaskGroup.Build;
    if (command === 'test') task.group = vscode.TaskGroup.Test;
    return task;
}

// A project in the workspace wins. A file outside any project is run as a script.
function currentTarget() {
    const editor = vscode.window.activeTextEditor;
    if (editor && editor.document.languageId === 'wood' && editor.document.uri.scheme === 'file') {
        const file = editor.document.uri.fsPath;
        let directory = path.dirname(file);
        for (;;) {
            if (fs.existsSync(path.join(directory, 'wood.json'))) return { folder: directory };
            const parent = path.dirname(directory);
            if (parent === directory) return { file };
            directory = parent;
        }
    }
    return {};
}

async function runCommand(command) {
    const target = currentTarget();
    const wood = findWood();
    if (!wood.file) {
        explainMissing(wood.problem);
        return;
    }
    if (!target.file && !target.folder && !vscode.workspace.workspaceFolders?.length) {
        vscode.window.showInformationMessage('wood: open a .wd file or a folder with a wood.json first.');
        return;
    }
    await vscode.workspace.saveAll(false);
    const task = createTask(command, target.file);
    if (target.folder) task.execution = new vscode.ProcessExecution(wood.file, [command], { cwd: target.folder });
    await vscode.tasks.executeTask(task);
}

function activate(context) {
    output = vscode.window.createOutputChannel('wood');
    statusItem = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Right, 10);
    context.subscriptions.push(output, statusItem);
    context.subscriptions.push(vscode.commands.registerCommand('wood.restartServer', startServer));
    context.subscriptions.push(vscode.commands.registerCommand('wood.showOutput', () => output.show()));
    for (const command of COMMANDS) {
        context.subscriptions.push(vscode.commands.registerCommand(`wood.${command}`, () => runCommand(command)));
    }
    context.subscriptions.push(vscode.tasks.registerTaskProvider('wood', {
        provideTasks() {
            if (!findWood().file) return [];
            const tasks = [];
            for (const folder of vscode.workspace.workspaceFolders ?? []) {
                if (fs.existsSync(path.join(folder.uri.fsPath, 'wood.json'))) {
                    for (const command of COMMANDS) tasks.push(createTask(command, undefined, [], folder));
                }
            }
            return tasks;
        },
        resolveTask(task) {
            const { command, file, args } = task.definition;
            if (!COMMANDS.includes(command) || !findWood().file) return undefined;
            return createTask(command, file, args ?? [], task.scope && task.scope.uri ? task.scope : undefined);
        },
    }));
    context.subscriptions.push(vscode.workspace.onDidChangeConfiguration((event) => {
        if (event.affectsConfiguration('wood.path') || event.affectsConfiguration('wood.languageServer.enabled')) startServer();
    }));
    context.subscriptions.push({ dispose: () => { stopServer(); } });
    return startServer().then(() => ({ findWood, isRunning: () => client?.isRunning() ?? false }));
}

function deactivate() {
    return stopServer();
}

module.exports = { activate, deactivate };

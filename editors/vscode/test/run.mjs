// Runs the extension inside a real VS Code, with a throwaway profile, so nothing touches the
// developer's own settings or extensions. Usage:
//   WOOD=path/to/wood CODE=path/to/Code.exe node test/run.mjs
import { spawn } from 'node:child_process';
import { cpSync, mkdtempSync, mkdirSync, realpathSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const wood = process.env.WOOD;
const code = process.env.CODE;
if (!wood || !code) {
    console.error('Set WOOD to the wood executable and CODE to the VS Code executable.');
    process.exit(2);
}
const work = realpathSync(mkdtempSync(join(tmpdir(), 'wood-vscode-')));
const profile = join(work, 'profile');
const project = join(work, 'project');
mkdirSync(join(profile, 'User'), { recursive: true });
writeFileSync(join(profile, 'User', 'settings.json'), JSON.stringify({
    'wood.path': resolve(wood),
    'security.workspace.trust.enabled': false,
    'update.mode': 'none',
    'telemetry.telemetryLevel': 'off',
    'extensions.autoCheckUpdates': false,
    'extensions.autoUpdate': false,
    'workbench.startupEditor': 'none',
}));
cpSync(join(here, 'fixture'), project, { recursive: true });
const environment = { ...process.env, WOOD_TEST_PROJECT: project };
delete environment.ELECTRON_RUN_AS_NODE;
const child = spawn(code, [
    project,
    `--user-data-dir=${profile}`,
    `--extensions-dir=${join(work, 'extensions')}`,
    `--extensionDevelopmentPath=${resolve(here, '..')}`,
    `--extensionTestsPath=${join(here, 'suite.js')}`,
    '--disable-gpu',
    '--disable-updates',
    '--disable-workspace-trust',
    '--skip-welcome',
    '--skip-release-notes',
    '--disable-extensions',
], { env: environment, stdio: 'inherit' });
const timer = setTimeout(() => {
    console.error('VS Code did not finish within five minutes.');
    child.kill();
}, 300000);
child.on('exit', (status) => {
    clearTimeout(timer);
    try {
        rmSync(work, { recursive: true, force: true, maxRetries: 5, retryDelay: 500 });
    } catch {
        // A lingering VS Code helper may hold a file for a moment. The directory is disposable.
    }
    console.log(status === 0 ? 'Extension tests passed.' : `Extension tests failed: ${status}`);
    process.exit(status ?? 1);
});

// Builds the .vsix into a directory outside the source tree. Publishing is a separate, manual step.
// Usage: node scripts/package.mjs OUTPUT_DIRECTORY
import { spawnSync } from 'node:child_process';
import { copyFileSync, existsSync, mkdirSync, readFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const repository = resolve(here, '..', '..');
const output = process.argv[2] && resolve(process.argv[2]);
if (!output) {
    console.error('Usage: node scripts/package.mjs OUTPUT_DIRECTORY');
    process.exit(2);
}
mkdirSync(output, { recursive: true });

// These come from the repository, so there is one copy of each to keep correct.
copyFileSync(join(repository, 'docs', 'assets', 'logo.png'), join(here, 'icon.png'));
copyFileSync(join(repository, 'LICENSE-MIT'), join(here, 'LICENSE-MIT'));
copyFileSync(join(repository, 'LICENSE-APACHE'), join(here, 'LICENSE-APACHE'));
copyFileSync(join(repository, 'LICENSE'), join(here, 'LICENSE'));

function run(command, args) {
    const shell = process.platform === 'win32';
    const result = spawnSync(command, args, { cwd: here, stdio: 'inherit', shell });
    if (result.status !== 0) process.exit(result.status ?? 1);
}
if (!existsSync(join(here, 'node_modules', 'vscode-languageclient'))) run('npm', ['ci', '--omit=dev']);
const { version } = JSON.parse(readFileSync(join(here, 'package.json'), 'utf8'));
const target = join(output, `wood-${version}.vsix`);
run('npx', ['--yes', '@vscode/vsce@3.6.0', 'package', '--out', `"${target}"`]);
console.log(target);

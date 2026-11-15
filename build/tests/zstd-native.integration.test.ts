import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { cp, mkdtemp, mkdir, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { test } from 'node:test';
import ProjectBuilder from '../main-builder.ts';

const staticLibrary = '/usr/lib/x86_64-linux-gnu/libzstd.a';
const sourceProject = resolve(import.meta.dirname, 'fixtures/fire-zstd');

test('links real Zstd statically into a Fire FFI shared library', {
  skip: process.platform !== 'linux' || process.arch !== 'x64' || !existsSync(staticLibrary) ||
    !existsSync(join(sourceProject, 'native')) || !existsSync(join(sourceProject, 'tests', 'native_smoke.c')),
}, async () => {
  const root = await mkdtemp(join(tmpdir(), 'clank-fire-test-'));
  try {
    await mkdir(join(root, 'native'));
    await mkdir(join(root, 'tests'));
    await cp(join(sourceProject, 'native'), join(root, 'native'), { recursive: true });
    await cp(join(sourceProject, 'tests', 'native_smoke.c'), join(root, 'tests', 'native_smoke.c'));
    const builder = new ProjectBuilder({ rootDir: root, buildDir: 'out', toolchain: 'gcc', profile: 'release' });
    builder.addSharedLibrary('fire_zstd', {
      sources: ['native/fire_zstd.c'], includeDirs: ['native'],
      defines: { FIRE_ZSTD_EXPORTS: 1, _POSIX_C_SOURCE: 200809 },
      cFlags: ['-Werror', '-Wpedantic', '-fvisibility=hidden'],
      libraries: [staticLibrary],
      linkFlags: ['-Wl,--exclude-libs,ALL'],
    });
    builder.addExecutable('smoke', {
      sources: ['tests/native_smoke.c'], includeDirs: ['native'],
      dependencies: ['fire_zstd'],
    });
    const result = await builder.build();
    assert.match(execFileSync(result.outputs.smoke, [], { cwd: root, encoding: 'utf8' }), /smoke test passed/);
    const dependencies = execFileSync('ldd', [result.outputs.fire_zstd], { encoding: 'utf8' });
    assert.doesNotMatch(dependencies, /libzstd\.so/);
    const exported = execFileSync('nm', ['-D', '--defined-only', result.outputs.fire_zstd], { encoding: 'utf8' });
    assert.match(exported, /fire_zstd_compress_file/);
    assert.doesNotMatch(exported, /\bZSTD_compressStream2\b/);
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

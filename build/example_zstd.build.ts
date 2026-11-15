/**
 * FireZstd: cross-compile vendored Zstandard using programmatic Clank targets.
 *
 * Layout:
 *   FireZstd/builder/{firezstd.build.ts,main-builder.ts,cli.ts,src/*}
 *   FireZstd/lib/{common,compress,decompress,dictBuilder,zstd.h,...}
 *
 * Run: bun run builder/firezstd.build.ts --release
 *
 * Edit ZSTD_TARGETS below to select the desired platforms. The command line
 * never selects an OS, CPU, ABI or Zig triple.
 */
import { existsSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import ProjectBuilder, { sourceTree } from './main-builder.ts';
import { parseCli, runBuildCli } from './cli.ts';

const ZSTD_TARGETS = [
  'x86_64-windows-gnu',
  'aarch64-windows-gnu',
  'x86_64-linux-gnu',
  'aarch64-linux-gnu',
  'x86_64-linux-musl',
  'aarch64-linux-musl',
  'x86_64-macos',
  'aarch64-macos',
] as const;

// Upstream static libraries disable multithreading by default.
const MULTITHREADED = false;
const ENABLE_X86_ASM = true;

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const parsed = parseCli(process.argv.slice(2));
const inspectionOnly = parsed === 'help' || parsed.doctor || parsed.clean;
if (!inspectionOnly) {
  for (const folder of ['common', 'compress', 'decompress', 'dictBuilder']) {
    if (!existsSync(join(root, 'lib', folder))) {
      throw new Error(`Missing lib/${folder}. Copy meta/zstd/lib into the project's lib directory.`);
    }
  }
}

const sources = [
  sourceTree('lib/common', { extensions: ['.c'] }),
  sourceTree('lib/compress', { extensions: ['.c'] }),
  sourceTree('lib/decompress', { extensions: ['.c'] }),
  sourceTree('lib/dictBuilder', { extensions: ['.c'] }),
];

const builder = new ProjectBuilder({
  rootDir: root,
  buildDir: 'build-zstd',
  toolchain: 'zig',
  profile: 'release',
  jobs: 8,
  includeDirs: ['lib', 'lib/common', 'lib/compress', 'lib/decompress', 'lib/dictBuilder'],
  defines: {
    XXH_NAMESPACE: 'ZSTD_',
    DEBUGLEVEL: 0,
    ZSTD_LEGACY_SUPPORT: 0,
    ZSTD_LIB_DEPRECATED: 0,
    ...(MULTITHREADED ? { ZSTD_MULTITHREAD: 1 } : {}),
  },
});

builder.addStaticLibrary('zstd', {
  sources,
  threads: MULTITHREADED,
});

for (const triple of ZSTD_TARGETS) {
  const x86_64 = triple.startsWith('x86_64-');
  const useAsm = ENABLE_X86_ASM && x86_64;
  builder.addCrossTarget(triple, {
    targets: {
      zstd: {
        sources: useAsm ? [...sources, 'lib/decompress/huf_decompress_amd64.S'] : sources,
        defines: useAsm ? {} : { ZSTD_DISABLE_ASM: 1 },
        // Position-independent code permits static linking into shared FFI libraries.
        pic: !triple.includes('-windows-'),
      },
    },
  });
}

runBuildCli(builder).catch(error => {
  console.error(error instanceof Error ? error.message : String(error));
  process.exitCode = 1;
});

/**
 * FireZstd: build upstream Zstandard as static and shared libraries using Zig.
 *
 * Place the contents of meta/zstd/lib/ into FireZstd/lib/.
 * Configure the destination matrix below; no command-line cross-target flags.
 * Run: bun run builder/firezstd.build.ts
 */
import { existsSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import ProjectBuilder, { sourceTree } from './main-builder.ts';
import { parseCli, runBuildCli } from './cli.ts';

// Change this list to choose the compilation targets.
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

const ENABLE_X86_ASM = true;
// Match upstream defaults: static builds are single-threaded; shared builds are multithreaded.
const MULTITHREADED_STATIC = false;
const MULTITHREADED_SHARED = true;

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

// Deliberately omit legacy/ and deprecated/ sources.
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
  },
});

// Keep objects separate: a shared library needs PIC and (on Windows) DLL exports.
// Both outputs use the upstream libzstd basename, but have different extensions.
builder.addStaticLibrary('zstd_static', {
  sources,
  outputName: 'zstd',
  threads: MULTITHREADED_STATIC,
  defines: MULTITHREADED_STATIC ? { ZSTD_MULTITHREAD: 1 } : {},
});

builder.addSharedLibrary('zstd_shared', {
  sources,
  outputName: 'zstd',
  threads: MULTITHREADED_SHARED,
  defines: MULTITHREADED_SHARED ? { ZSTD_MULTITHREAD: 1 } : {},
});

for (const triple of ZSTD_TARGETS) {
  const windows = triple.includes('-windows-');
  const useAsm = ENABLE_X86_ASM && triple.startsWith('x86_64-');
  const platformSources = useAsm
    ? [...sources, 'lib/decompress/huf_decompress_amd64.S']
    : sources;
  const asmDefines: Record<string, number> = useAsm ? {} : { ZSTD_DISABLE_ASM: 1 };

  builder.addCrossTarget(triple, {
    targets: {
      zstd_static: {
        sources: platformSources,
        defines: asmDefines,
        // PIC archives can also be linked into other shared libraries.
        pic: !windows,
      },
      zstd_shared: {
        sources: platformSources,
        // Upstream Windows builds use libzstd.dll and libzstd.dll.a.
        outputName: windows ? 'libzstd' : 'zstd',
        defines: {
          ...asmDefines,
          ...(windows ? { ZSTD_DLL_EXPORT: 1 } : {}),
        },
        pic: !windows,
        // Public Zstd API symbols explicitly retain default visibility on ELF/Mach-O.
        cFlags: windows ? [] : ['-fvisibility=hidden'],
      },
    },
  });
}

runBuildCli(builder).catch(error => {
  console.error(error instanceof Error ? error.message : String(error));
  process.exitCode = 1;
});

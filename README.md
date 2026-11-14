# Clank Build

**C/C++ builds expressed in ordinary TypeScript.** No CMake, Ninja, Make, or shell scripts are used by the build engine. Bun executes the TypeScript configuration; the engine invokes native compilers directly.

Based on the four original files (`main-builder.ts`, `arch.ts`, `extended.ts`, and `build-project.ts`), rewritten into a complete initial build system rather than incrementally patching the original unsafe command runner.

## Configure your own C/C++ project

```ts
import ProjectBuilder, { sourceTree } from './main-builder';
import { runBuildCli } from './cli';

const build = new ProjectBuilder({
  rootDir: process.cwd(),
  buildDir: 'build-ts',
  includeDirs: ['include'],
  toolchain: 'auto',
  profile: 'release',
  defines: { FEATURE_FAST_MATH: 0 },
});

build.addStaticLibrary('mathcore', {
  sources: [sourceTree('libraries/mathcore', { extensions: ['.c', '.cpp'] })],
  includeDirs: ['libraries/mathcore/include'],
});

build.addExecutable('game', {
  sources: [sourceTree('src', { exclude: ['**/tests/**'] })],
  dependencies: ['mathcore'],
  outputName: 'game',
});

runBuildCli(build, 'game').catch(error => {
  console.error(error);
  process.exitCode = 1;
});
```

TypeScript is the actual project configuration language. You can conditionally add targets/flags, read environment variables, generate C files, perform filesystem operations, or import your own helper modules.

### Code-generation tasks

```ts
build.addTask({
  name: 'generate-version',
  inputs: ['VERSION'],
  outputs: ['include/generated_version.h'],
  version: 'v1',
  async action({ rootDir }) {
    const { readFile, writeFile } = await import('node:fs/promises');
    const { join } = await import('node:path');
    const version = (await readFile(join(rootDir, 'VERSION'), 'utf8')).trim();
    await writeFile(join(rootDir, 'include/generated_version.h'),
      `#define PROJECT_VERSION "${version}"\n`);
  },
});

build.addExecutable('app', {
  sources: ['src/main.c'],
  tasks: ['generate-version'],
});
```

Tasks can depend on other tasks, cache their input hashes, and must produce declared outputs. An output-less task runs on every build. Each task may invoke a subprocess safely through `context.run(argv)`. Rebuild triggers source/header dependency invalidation automatically.

## Features implemented

- Configurable **multiple targets** (executables, static/shared libraries) and dependency graphs, with cycle detection.
- Native C11 compilation, C++20, ASM files for GCC/Clang; toolchain-specific compiler and linker flags.
- Automatic Windows MSYS2 CLANG64 / UCRT64 / MINGW64 discovery plus optional MSVC, clang-cl, Clang and GCC.
- Parallel compile scheduling with bounded concurrency and failure propagation.
- **Content-based incremental builds**: compile command, toolchain identity, C/C++ source and actual transitive compiler-reported headers are hashed. GCC/Clang use `-MMD` depfiles. MSVC uses `/showIncludes`; a conservative project-header scan is used if localized include output cannot be parsed.
- Link caching and static archive replacement (no stale archive members after removing sources).
- Unique object paths based on source path digests; duplicate C filenames are safe.
- Build profiles: debug, release, relwithdebinfo. Optional GCC/Clang LTO (thin/full for Clang), MSVC LTCG.
- Automatic response files for long linker/archive commands; helpful for large MSVC projects.
- Cross-platform output names and runtime rpaths for directly dependent Unix shared libraries. Windows DLLs are emitted alongside `.exe` outputs with import libraries under `lib/`.
- `compile_commands.json` generated alongside the build. No global prototype modifications or monkey patches.
- Programmatic API plus a small CLI (`--run`, `--clean`, `--jobs`, `--target`, `--rebuild`, `--dry-run`, `--verbose`, etc.).
- Strict failures on compiler errors; never silently report a successful build after a failed compiler invocation.

## Design/limits (explicit)

- **Native builds only** at present. The `arch.ts` detector reports the *host* architecture; selecting a cross-compiler triple/SDK/target sysroot is a separate feature.
- PGO training/orchestration, BOLT, signed Windows resource/icon compilation, precompiled headers, unity builds, and remote/distributed caches are **not implemented yet**. Because build settings accept arbitrary compiler and linker flags, you can implement these workflows in TypeScript build scripts, but they are not automatic CLI modes.
- Incremental MSVC include discovery assumes English `/showIncludes` diagnostics (`VSLANG=1033`); when they are not available, all project headers under configured include roots are tracked conservatively, sacrificing some incremental granularity for correctness.
- The v2 build engine is **tested with Linux Clang and GCC, and Windows with MinGW-w64**.
- Projects with unusual source generation/configuration may need explicit tasks and feature defines matching their original build systems. The PocketPy example reproduces core upstream flags, but does not enable optional modules such as periphery.
- Do not put a build directory outside the project or point it at the project root. `clean()` refuses unsafe output locations.

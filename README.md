# Bruga Build

**C/C++ builds expressed in ordinary TypeScript.** No CMake, Ninja, Make, or shell scripts are used by the build engine. Bun executes the TypeScript configuration; the engine invokes native compilers directly.

Based on the four original files (`main-builder.ts`, `arch.ts`, `extended.ts`, and `build-project.ts`), rewritten into a complete initial build system rather than incrementally patching the original unsafe command runner.

## Compilar PocketPy en Windows sin Visual Studio

La versión 0.2 ya **no necesita MSVC**. El modo `auto` intenta, por orden:

1. Clang de MSYS2 **CLANG64** (`clang-mingw`, ABI GNU/UCRT).
2. GCC de **UCRT64/MINGW64** (`mingw`).
3. Visual Studio MSVC, si está instalado.
4. `clang-cl`, si existe el entorno MSVC necesario.

El builder nunca mezcla accidentalmente `clang.exe` de una distribución con `llvm-ar.exe` o `ar.exe` de otra. Comprueba el `-dumpmachine` para rechazar los compiladores POSIX `MSYS` y Clang configurado para `windows-msvc` cuando se pide ABI GNU. Un Clang independiente instalado en `C:\Program Files\LLVM` **no equivale** a un SDK MinGW completo: necesita runtime CRT, headers, bibliotecas y linker apropiados. La forma más sencilla es instalar MSYS2 CLANG64.

### Instalación recomendada (MSYS2)

En una terminal **MSYS2 CLANG64**:

```sh
pacman -Syu
pacman -S mingw-w64-clang-x86_64-clang
```

Si `pacman -Syu` pide reiniciar la terminal, ciérrala, vuelve a abrirla y completa la actualización antes de instalar paquetes.

Para GCC abre **MSYS2 UCRT64**:

```sh
pacman -Syu
pacman -S mingw-w64-ucrt-x86_64-gcc
```

Las instalaciones habituales se encuentran en `C:\msys64\clang64\bin` y `C:\msys64\ucrt64\bin`. Los paths se detectan aunque no estén en `PATH`; también se admiten `MSYS2_ROOT`, `LLVM_ROOT`, `MINGW_ROOT` y prefijos alternativos.

### Primer diagnóstico: **sin compilar nada**

Ejecuta PowerShell desde la raíz de PocketPy:

```powershell
bun run builder/pocketpy.build.ts --doctor
```

El diagnóstico dice qué toolchains se han encontrado y desde qué carpetas. No requiere ningún compilador y no ejecuta CMake.

Si no encuentra nada, indica la carpeta exacta:

```powershell
bun run builder/pocketpy.build.ts --doctor --toolchain-dir "C:\msys64\clang64\bin"
```

### Compilación

```powershell
bun run builder/pocketpy.build.ts --release --compiler clang-mingw
bun run builder/pocketpy.build.ts --release --compiler clang-mingw --run
bun run builder/pocketpy.build.ts --release --compiler mingw
bun run builder/pocketpy.build.ts --release --compiler mingw --toolchain-dir "C:\msys64\ucrt64\bin"
```

La salida habitual con CLANG64 es:

```text
build-ts/release-clang-mingw-x64/bin/pocketpy.exe
```

`--compiler clang` y `--compiler gcc` también sirven como alias en Windows (`clang-mingw` y `mingw`, respectivamente). No se requieren bash, Make, Ninja ni CMake.

Cuando se ejecuta con `--run`, el directorio de los binarios del toolchain se agrega temporalmente a `PATH` para que Windows encuentre sus DLL de runtime. Para ejecutar el `.exe` manualmente, usa un terminal MSYS2 del entorno correcto o agrega esa carpeta a tu `PATH`.

El script de PocketPy declara la biblioteca estática `pocketpy` con `src/**/*.c` y la CLI con `src2/main.c`. No compila el JIT nativo (SLJIT); `jitbridge.c` permanece porque es una interfaz del intérprete.

**Limitación de validación:** la v2 ha superado tests de compilación y enlace reales en Linux con GCC/Clang y simulaciones de detección de toolchains de Windows. No se ha podido compilar un ejecutable PE/COFF real ni tu árbol completo modificado de PocketPy en este entorno; por tanto, pueden aparecer errores de integración ajenos al descubrimiento de compiladores.

## Otros comandos

```powershell
bun run builder/pocketpy.build.ts --debug --compiler mingw
bun run builder/pocketpy.build.ts --release --compiler clang-mingw --lto thin
bun run builder/pocketpy.build.ts --release --jobs 12 --rebuild
bun run builder/pocketpy.build.ts --clean
bun run builder/pocketpy.build.ts --help
```

Cada perfil, toolchain y configuración LTO tiene un directorio de salida propio. Para borrar outputs, `--clean` sólo opera en carpetas marcadas como propiedad del builder.

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
- The v2 build engine is **tested with Linux Clang and GCC, and Windows discovery is simulated in tests**. Windows toolchain handling is implemented but could not be exercised in this runtime. Do not treat it as Windows CI validation.
- Projects with unusual source generation/configuration may need explicit tasks and feature defines matching their original build systems. The PocketPy example reproduces core upstream flags, but does not enable optional modules such as periphery.
- Do not put a build directory outside the project or point it at the project root. `clean()` refuses unsafe output locations.

## Tests

Bun is the intended runtime; development type-checking/tests can also use Node.js after TypeScript transpilation.

```sh
npm install --no-audit --no-fund
npm test
```

`npm test` runs `tsc` and the built-in Node test runner, compiling and linking **real C/C++ fixtures**. Runtime dependencies are **zero**: Bun runs the `.ts` files directly without `npm install`. Node/npm are only used for offline validation in environments without Bun.

The covered cases include: compile/link, incremental noop, changes in `#include` headers, duplicate basenames, command flag changes, failed compilation recovery, cleaning, circular dependencies, Clang and GCC, C++ runtime linkage, TypeScript code-generation tasks, shared-library rpaths, and response files with spaces.

## File layout

```text
builder/
  arch.ts                  host architecture helper
  extended.ts              pure utilities (no prototype mutation)
  main-builder.ts          ProjectBuilder API and build engine
  cli.ts                   CLI adapter for .build.ts scripts
  src/types.ts             typed targets and configuration
  src/toolchains.ts        compiler discovery and MSVC initialization
  src/process.ts           safe subprocess runner / parallel worker pool
  src/depfile.ts           dependency files and SHA-256 content hashes
  src/sources.ts           source discovery and glob exclusions
  build-project.ts         generic example replacing the original example
  pocketpy.build.ts        PocketPy-specific project definition
  tests/builder.test.ts    compiler integration and simulated Windows detection tests
  package.json
  tsconfig.json
  README.md
```

All source-code comments are in English.

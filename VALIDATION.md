# Bruga Build v0.2 validation

- `tsc --noEmit` strict type check: PASS.
- `node --test dist/tests/*.test.js`: 17/17 PASS.
- Real GCC and Clang C11 compile + link: PASS (Linux).
- C++ executable, static and shared libraries: PASS (Linux).
- Incremental source/header rebuilds, command changes, clean, failures, LTO, tasks, response files: PASS (Linux).
- `node dist/pocketpy.build.js --doctor`: PASS without CMake (Linux).
- Windows discovery scenarios with mock `.exe` commands: CLANG64, UCRT64, explicit path, invalid MSYS GCC, no mixed toolchains: PASS (simulated).
- Real MSYS2 Windows toolchain compilation and the user's modified PocketPy checkout: NOT TESTED in this environment.

The Windows tests validate detection logic only; they do not imply successful native PE/COFF linking or runtime DLL resolution on Windows.

# Validation report

Verified in this sandbox on Linux/x86-64:

- TypeScript compiler: `tsc 5.8.3` with `strict`, `noUnusedLocals`, and `noUnusedParameters`.
- Native C toolchains: Clang 17 and GCC 14.2.
- **13/13 integration tests passed** via `node --test` on transpiled TypeScript.
- Real C11 compile + static archive + executable link + execute.
- C++20 compile + standard-library runtime link + execute.
- SHA-256 incremental no-op, header edits, one-source edits, and flag-change rebuilds.
- Clang ThinLTO compile/archive/link/execute.
- Shared-library linking and runtime loader rpath.
- Executable with duplicate source basenames.
- Parallel compilation, compiler-error propagation, and recovery.
- User-authored TypeScript code-generation task and dependency cache invalidation.
- Toolchain selection for GCC and Clang.
- Linker/archiver response files with paths containing spaces.
- PocketPy build-script **source-layout smoke** on a synthetic `src` + `src2` tree, including executing the produced binary and an incremental no-op.

Not verified in this environment:

- The Bun runtime itself (not installed here; Node transpilation used for tests).
- MSVC/clang-cl Windows toolchains and resource/icon files.
- The actual user's modified PocketPy sources (not supplied with these four TypeScript files).
- PocketPy runtime semantics or speedups from the prior interpreter patches.
- Complete PGO or BOLT workflows, cross-compilation, precompiled headers, remote caching.

The builder is ready for local smoke tests in the user's PocketPy checkout. If native source integration is incomplete, the tool will report real compiler errors rather than suppress them.

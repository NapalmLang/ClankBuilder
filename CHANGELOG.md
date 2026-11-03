# Changes

## v0.2.0 — Windows LLVM/Clang + MinGW-w64 support

- Reworked Windows `auto` toolchain detection: Clang GNU → MinGW-w64 GCC → MSVC → clang-cl.
- Discover MSYS2 CLANG64, UCRT64, MINGW64, alternate installation roots, environment overrides and explicit `--toolchain-dir`.
- Reject MSYS POSIX or Windows-MSVC-targeted compilers when a Windows GNU ABI compiler is required.
- Do not combine compiler, C++ driver and archiver from unrelated prefixes.
- Add `--doctor` that works without installed compilers.
- Repair Windows GNU `.dll.a` import-library link dependencies, Windows runner PATH, and GNU UTF-8 response files (rather than MSVC UTF-16LE).
- Enable Clang Thin/Full LTO for the `clang-mingw` toolchain.
- PocketPy example uses no MSVC-only `/fp:` flag when the selected compiler is GNU.
- Add simulated Windows discovery regressions; 17 integration tests pass on Linux.

# Changes from the uploaded prototype

The original files have been preserved only in the development workspace; they are not part of the deliverable ZIP. The replacement is written from the same premise: the build instructions are TypeScript itself.

- `main-builder.ts`: rebuilt as a graph-aware multi-target engine with incremental C/C++ compilation, metadata caching, linking, archiving, toolchain-aware arguments, and error propagation.
- `arch.ts`: retained host architecture detection and Rosetta support; exposed normalization as a named export.
- `extended.ts`: replaced `Array.prototype`, `Set.prototype`, and `String.prototype` monkey-patches with safe, importable pure helpers. SHA-256 replaces the collision-prone custom 32-bit string hash for cache keys.
- `build-project.ts`: simplified and modernized as a standalone example of executable-target configuration.
- `pocketpy.build.ts`: added a dedicated C11 static-library + CLI build for the PocketPy source tree, without needing SLJIT.
- `src/`: added separately testable, typed modules for command execution, toolchains, dependency tracking, and source discovery.
- `cli.ts`: added user-facing options and an API for running the built program.
- `tests/`: real compilations, not just mocks.

The original tool used `exec(commandString)` with no robust quoting and swallowed errors; header files could be compiled as objects and output names could collide. The new engine runs compilers as `spawn(exe, argv)` and stops on errors.

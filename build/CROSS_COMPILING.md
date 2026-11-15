# Clank Builder: programmatic Zig cross-compilation

Clank's build plans are written in TypeScript. Install [Zig](https://ziglang.org/download/) and put `zig` on `PATH`. **No target/OS/architecture CLI flag is used.** Register each platform directly in the project script with `builder.addCrossTarget(triple)`.

```ts
import ProjectBuilder from './main-builder.ts';

const builder = new ProjectBuilder({
  rootDir: process.cwd(),
  buildDir: 'build',
  toolchain: 'zig',
  profile: 'release',
});

builder.addStaticLibrary('core', { sources: ['src/core.c'] });

builder.addCrossTarget('x86_64-windows-gnu');
builder.addCrossTarget('aarch64-windows-gnu');
builder.addCrossTarget('x86_64-linux-musl');
builder.addCrossTarget('aarch64-linux-musl');
builder.addCrossTarget('x86_64-macos');
builder.addCrossTarget('aarch64-macos');

const result = await builder.build();
console.log(result.crossTargets?.['aarch64-macos'].outputs.core);
```

`build()` builds every registered platform, in the order it was registered. When no platforms are registered, `build()` continues to build natively as before. All platforms have independent objects, depfiles, compile commands and caches (`build/release-zig-<triple>/`). `build().crossTargets` maps each triple to the original `BuildResult`; for convenience `build().outputs` also contains entries keyed by `<triple>:<target-name>`.

Targets can customize a C/C++ library per platform without duplicating the project definition:

```ts
builder.addCrossTarget('aarch64-macos', {
  targets: {
    core: {
      defines: { DISABLE_X64_ASSEMBLY: 1 },
      sources: ['src/core.c', 'src/platform/arm64.c'],
      pic: true,
    },
  },
});
```

`defines` are merged and flags/libraries are appended; `sources`, `pic` and `threads` replace the original target values. The `--target NAME` CLI option still selects a C/C++ *project target* (not a CPU or operating system), and will build it for **all** registered platforms. `--compiler`, `--release`, `--jobs`, `--dry-run`, `--clean` and `--doctor` retain their ordinary meanings. `--cross-target` is not a CLI option. `--run` is rejected for build matrices to avoid mistakenly executing a foreign binary. The output compiler always uses Zig for registered cross targets.

## FireZstd

Place the **contents** of [`meta/zstd/lib`](https://github.com/meta/zstd/tree/dev/lib) under `FireZstd/lib/`, and copy `firezstd.build.ts` alongside the Clank Builder sources in `FireZstd/builder/`. The `ZSTD_TARGETS` constant controls exactly which platforms are built. Edit it to add or remove destinations, then run:

```sh
bun run builder/firezstd.build.ts
```

The project generates static `libzstd.a` archives using `zig cc` and `zig ar`. Zstd's `common`, `compress`, `decompress` and `dictBuilder` directories are included; `legacy` and `deprecated` are excluded. Legacy format decoding is disabled via `ZSTD_LEGACY_SUPPORT=0`. Optional x86_64 Huffman assembly is enabled only for x86_64 targets; the others define `ZSTD_DISABLE_ASM=1`. Threading defaults to disabled, matching upstream's static library behavior; edit the `MULTITHREADED` constant to enable it (consumers must then link with the required thread libraries).

## Platform considerations

- These examples build GNU-ABI Windows archives. Linking a library into an MSVC project may require a dedicated ABI-compatible build.
- Zig's target and libc support can vary with compiler version. Some combinations may require a native SDK or sysroot for application linking; creating a static archive does not prove that a complete executable will link.
- Test the archives in CI on each actual platform, especially Windows ARM64 and macOS. A successful dry run only validates the build plan.
- Zig does not produce macOS universal/fat archives automatically. Package the x86_64 and ARM64 Mach-O archives separately or merge with `lipo` in a dedicated packaging step.
- Pin an upstream Zstd release or Git commit and preserve its BSD license when vendoring sources.

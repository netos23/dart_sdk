# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build System

This is the Dart SDK. It uses GN + Ninja (via wrapper scripts), not CMake or Make directly.

**Prerequisites:** Python 3, XCode (macOS), and Chromium's `depot_tools` in `PATH`.

```bash
# From the sdk/ directory root
./tools/build.py --mode release create_sdk
```

Output lands in `xcodebuild/ReleaseARM64/dart-sdk` on macOS, `out/ReleaseX64/dart-sdk` on Linux/Windows.

Build only the VM runtime (faster for VM-only changes):
```bash
./tools/build.py --mode release runtime
```

Build debug + release:
```bash
./tools/build.py --mode all runtime
```

AOT precompiled runtime:
```bash
./tools/build.py --mode release runtime_precompiled
```

## Testing

All tests go through `tools/test.py`:

```bash
# Run all VM tests (release mode)
./tools/test.py -mrelease --runtime=vm

# Run a specific test suite
./tools/test.py -mrelease --runtime=vm corelib

# Run a single test
./tools/test.py -mrelease --runtime=vm corelib/ListTest

# Run lib/module tests
./tools/test.py -mrelease --runtime=vm lib/module

# AOT precompilation tests
./tools/test.py -mrelease -cprecompiler -rdart_precompiled
```

Build required targets before testing:
```bash
./tools/build.py --mode release most run_ffi_unit_tests
```

## Architecture Overview

### Key directories

- `runtime/vm/` — Dart VM core: object model, snapshots, JIT/AOT compiler, GC
- `runtime/lib/` — C++ implementations of core library native methods (one `.cc` per library)
- `sdk/lib/` — Dart source for core/platform libraries (`dart:core`, `dart:io`, etc.)
- `sdk/lib/_internal/vm/lib/` — VM-specific `@patch` implementations for those libraries
- `pkg/` — Dart tools and packages (analyzer, compiler backends, test_runner, etc.)
- `tests/` — Test corpus; `tests/lib/` for library tests, `tests/language/` for language tests

### How dart: libraries are wired up

Each `dart:X` library has three layers:
1. **Public API** — `sdk/lib/X/X.dart` (Dart, `external` keyword for native methods)
2. **VM patch** — `sdk/lib/_internal/vm/lib/X_patch.dart` (fills in `@patch` implementations)
3. **Native entry** — `runtime/lib/X.cc` (C++ `DEFINE_NATIVE_ENTRY` handlers)

Registration flows through:
- `sdk/lib/libraries.yaml` and `sdk/lib/libraries.json` — library URI → file mapping per platform
- `sdk/lib/_internal/sdk_library_metadata/lib/libraries.dart` — metadata (categories, maturity)
- `runtime/vm/bootstrap_natives.h` — `V(MethodName, arg_count)` macro declarations
- `runtime/lib/X_sources.gni` — lists the `.cc` file for the GN build

### Snapshot kinds (`runtime/vm/snapshot.h`)

| Kind | Description |
|------|-------------|
| `kFull` | Full heap snapshot (JIT kernel) |
| `kFullJIT` | Full + JIT code |
| `kFullAOT` | Full + AOT compiled code (standard ELF binary) |
| `kFullAOTModule` | Full + AOT code compiled as a loadable module (`.so`) |

`IsAOT(kind)` returns true for both `kFullAOT` and `kFullAOTModule`. When adding new snapshot-field ranges to raw objects (the `to_snapshot()` methods in `runtime/vm/raw_object.h`), `kFullAOTModule` must be handled alongside `kFullAOT`.

### dart:module (this branch: `modular_aot`)

`dart:module` is a new **experimental** library that allows AOT-compiled Dart code to dynamically load other AOT snapshots compiled as shared libraries (`kFullAOTModule` kind).

Key files:
- `sdk/lib/module/module.dart` — public API (`Module`, `ModuleSource`, `FileModuleSource`)
- `sdk/lib/_internal/vm/lib/module_patch.dart` — VM patch wiring natives via `@pragma("vm:external-name", ...)`
- `runtime/lib/module.cc` — `Module_load` native: dlopen + snapshot validation + `ObjectStore` registration
- `runtime/vm/object_store.h` / `.cc` — four per-isolate-group arrays: `modules`, `module_meta_tables`, `module_dispatch_table_code_entries`, `module_instructions_tables`
- `tests/lib/module/module_loads_test.dart` — smoke test

`Module_load` is only active under `DART_PRECOMPILER && !TESTING`; other platforms/builds hit a stub that throws `UnsupportedError`. The library is `EXPERIMENTAL` maturity.

The load flow in `Module_load`:
1. Resolve `FileModuleSource.path` via reflection on `dart:module` classes
2. `dlopen` the `.so`
3. Resolve `kIsolateSnapshotDataCSymbol` / `kIsolateSnapshotInstructionsCSymbol`
4. Validate snapshot header and `kFullAOTModule` kind
5. Register a `ModuleSnapshotHandle` in a process-global `GrowableArray`
6. Construct a `Module` instance (sets `_nativeId`) and append it + empty tables to the four `ObjectStore` arrays
This package contains experimental dynamic modules API.

## Status: experimental

**NOTE**: This package is currently experimental and not published or
included in the SDK.

Do not take dependency on this package unless you are prepared for
breaking changes and possibly removal of this code at any point in time.

## VM modular compilation and dynamic loading plan

To support modular compilation in the VM pipeline and runtime loading of
compiled modules into the same isolate group (with visibility for all isolates
in that group), implement the work in these stages:

1. **Compiler entrypoints and module boundaries**
   - Extend bytecode compilation flow to explicitly model:
     - host/app compilation output;
     - module compilation output against host interface.
   - Keep using imported dill boundaries so module output contains only newly
     compiled libraries.

2. **Dynamic module validation at compile time**
   - Validate module API against host dynamic interface before bytecode
     generation.
   - Ensure diagnostics clearly identify forbidden cross-module inheritance,
     override, and callable-surface violations.

3. **Runtime module loading into isolate group**
   - Load module bytecode under isolate-group program lock.
   - Register new libraries in isolate-group-wide library tables.
   - Finalize loading so all isolates in the group can resolve newly loaded
     symbols.

4. **Embedder/VM API contract**
   - Keep `Dart_LoadLibraryFromBytecode`/`Dart_FinalizeLoading` as the required
     sequence for module activation.
   - Document that loading is isolate-group scoped, not isolate-local.

### Key code locations to update

- **Bytecode compiler / modular build path**
  - `pkg/dart2bytecode/lib/dart2bytecode.dart`
  - `pkg/vm/lib/kernel_front_end.dart`
  - `pkg/front_end/lib/src/kernel/dynamic_module_validator.dart`

- **Runtime module loading / isolate-group visibility**
  - `runtime/vm/dart_api_impl.cc` (`Dart_LoadLibraryFromBytecode`,
    `Dart_FinalizeLoading`)
  - `runtime/vm/bytecode_reader.cc` (`ReadLibraryDeclarations`)
  - `runtime/vm/object_store.*` (group-level library/loaded-unit registries)

- **Coverage / regression tests**
  - `pkg/dynamic_modules/test/runner/*`
  - `runtime/tests/vm/dart/exported_symbols_test.dart`

# Typed Direct Calls for `dart:module`

This document describes an architecture for typed direct calls between a host
AOT program and dynamically loaded `dart:module` AOT modules.

The design intentionally moves away from the earlier model where every module
owns an independent copy of all of its dependencies. Typed direct calls require
shared runtime identity for the API surface that crosses the host/module
boundary. The practical model is:

```text
host-owned ABI universe:
  shared libraries, classes, members, types, selector ids, selector offsets

module-owned implementation universe:
  private module libraries, private classes, private functions, private statics
```

Anything that can be mentioned in typed code on both sides must belong to the
host-owned ABI universe. Module-private objects can still cross the boundary as
`Object` or `dynamic`, or as implementations of shared ABI interfaces.

## Goals

- Allow host Dart code to directly call module code through typed APIs.
- Allow module Dart code to directly call host code through typed APIs.
- Preserve normal AOT call shapes where possible:
  - static/direct calls for known functions;
  - global dispatch table calls for interface and virtual calls;
  - closure calls for closure values.
- Preserve Dart type semantics across the boundary:
  - `is` and `as`;
  - generic bounds and type arguments;
  - method override dispatch;
  - field layout assumptions.
- Keep module loading deterministic and validate incompatibilities at load time.
- Reuse existing dynamic-module annotations where they already describe the same
  external extension and override constraints.

## Non-Goals

- Loading arbitrary modules compiled against a different SDK, VM configuration,
  or ABI manifest.
- Allowing two different runtime `Class` objects to represent the same shared
  API class.
- Making all module-private classes statically nameable from the host. If the
  host names a class statically, that class is ABI.
- Supporting speculative JIT-style deoptimization in precompiled AOT code.

## Current State

The current `dart:module` implementation is an explicit AOT module
loading/calling path plus ABI scaffolding. It does not yet provide transparent
typed direct calls between independently compiled host and module code.

Implemented today:

- `app-aot-module` snapshot generation in `bin/gen_snapshot.cc`.
- Public API entry points in `include/dart_api.h`:
  - `Dart_PrecompileAsModule`;
  - `Dart_CreateModuleAOTSnapshotAsBinary`.
- Runtime loading through `lib/module.cc`.
- `Snapshot::kFullAOTModule`.
- A VM ABI sidecar in `vm/module_abi.{h,cc}`:
  - fixed `"DMAB"` header, currently format version 2;
  - SDK hash, compiler-flags hash, target architecture, target OS, mode flags;
  - optional manifest hash;
  - runtime-id payload for private class count, private selector count, and
    serialized module dispatch table entry count.
- `ImageWriter::WriteModuleAbiData` emits the ABI sidecar as
  `kDartModuleAbiData` for module AOT images.
- `ProgramSerializationRoots` serializes the host/module ABI manifest hash and
  dispatch selector count. `ProgramDeserializationRoots` restores both and
  seeds the next module-private selector id.
- `Precompiler::FinalizeDispatchTable` records the dispatch table generator's
  selector count in the `IsolateGroup`.
- Module deserialization through
  `FullSnapshotReader::ReadModuleSnapshot` in `vm/app_snapshot.cc`.
- Per-module runtime state in `LoadedModule` in `vm/isolate.h`, including:
  - dynamic library handle and snapshot image pointers;
  - ABI header/runtime-id data;
  - module object store;
  - module static field tables;
  - deserialized module class tracking;
  - class-id binding table;
  - selector binding table;
  - export and future link tables;
  - dispatch table reservation state.
- `ReadModuleSnapshot` currently:
  - holds `program_lock` write while registering/deserializing a module;
  - validates the normal snapshot header/features;
  - reserves a module id;
  - shifts module-private class CIDs above the host CID range;
  - reserves class table slots from the ABI runtime-id payload;
  - reserves module-private selector ids after the host selector count;
  - preallocates the expected merged dispatch table size when declared;
  - validates serialized class, selector, and dispatch table counts where
    runtime-id data is available.
- `ModuleDeserializationRoots` reads into a module-specific object store, reads
  module static field tables, installs the module dispatch table contribution,
  records deserialized module classes, and builds a flat export table for
  surviving top-level static fields, regular functions, and getters.
- `Deserializer::ReadModuleDispatchTable` still merges by the serialized table
  layout plus CID offset. It can reuse the preallocated table from
  `LoadedModule`, but it does not yet merge by semantic selector id/key.
- `LoadedModule::VisitObjectPointers` visits the module object store, static
  field tables, class list, ABI import/export/link tables, selector bindings,
  and class-id bindings.
- Public Dart API currently includes:
  - `Module.load`;
  - `Module.getValue<T>`;
  - `Module.lookupFunction<T>`;
  - `Module.invokeMethod<T>`;
  - `Module.invokeStaticMethod<T>`;
  - `Module.invokeConstructor<T>`.
- `Module.lookupFunction<T>` returns an implicit closure for a retained
  top-level regular module function. It is an explicit lookup path, not a
  generated direct-call/link-slot path.
- `vm/module_abi_test.cc` covers ABI header/runtime-id encoding, compatibility
  checks, and export-table layout constants.
- `pkg/vm/lib/module_abi_manifest.dart` provides the Stage 1 deterministic
  JSON manifest envelope and 63-bit hash used by tooling. It can also convert
  the front end's detailed dynamic-interface dump into semantic library, class,
  and member ABI entries with callable, extendable, and can-be-overridden flags.
- `dart compile` now has Stage 1 manifest plumbing:
  - host AOT outputs can use `--emit-module-abi=<path>`;
  - module AOT outputs can use `--module-abi=<path>`;
  - both paths pass the manifest hash to `gen_snapshot` through
    `--module_abi_manifest_hash`.
  - if host compilation also supplies a dynamic interface to the kernel front
    end, `--emit-module-abi` captures the detailed dynamic-interface dump and
    includes those semantic entries in the manifest.
- `tests/lib/module/module_loads_test.dart` documents the current explicit
  `Module` API semantics and the no-module-image limitation. It can run against
  a prebuilt module by passing `-Dmodule.path=<path>`.

Important current code anchors:

- `lib/module.cc`
  - `Module_load`
  - `Module_getValue`
  - `Module_lookupFunction`
  - `Module_invokeMethod`
  - `Module_invokeStaticMethod`
  - `Module_invokeConstructor`
- `vm/module_abi.h`
  - `ModuleAbiHeader`
  - `ModuleAbiRuntimeIds`
  - `ModuleAbi`
  - `ModuleExportTable`
- `vm/module_abi.cc`
  - `ModuleAbi::ReadHeader`
  - `ModuleAbi::ReadRuntimeIds`
  - `ModuleAbi::WriteHeaderAndRuntimeIds`
  - `ModuleAbi::ValidateCompatibility`
- `vm/image_snapshot.{h,cc}`
  - `ImageWriter::WriteModuleAbiData`
  - `AssemblyImageWriter::WriteModuleAbiData`
  - `BlobImageWriter::WriteModuleAbiData`
- `vm/app_snapshot.cc`
  - `BuildModuleExportTable`
  - `ModuleDeserializationRoots`
  - `ReserveModuleClassTableSlots`
  - `ReserveModuleSelectorIds`
  - `ReserveModuleDispatchTableSlots`
  - `Deserializer::ReadModuleDispatchTable`
  - `FullSnapshotReader::ReadModuleSnapshot`
- `vm/isolate.h`
  - `LoadedModule`
  - `IsolateGroup::AddLoadedModule`
  - `IsolateGroup::GetLoadedModule`
  - `IsolateGroup::SetModuleAbiSelectorCount`
  - `IsolateGroup::ReserveModuleSelectorIds`
- `vm/compiler/aot/precompiler.cc`
  - `Precompiler::FinalizeDispatchTable`
- `vm/compiler/aot/dispatch_table_generator.h`
  - exposes selector count and table size for ABI scaffolding.
- `pkg/vm/lib/module_abi_manifest.dart`
  - `DartModuleAbiManifest`
- `pkg/dartdev/lib/src/commands/compile.dart`
  - `--emit-module-abi`
  - `--module-abi`
- `pkg/vm/lib/transformations/type_flow/table_selector_assigner.dart`
  - assigns selector ids used by AOT table dispatch.
- `vm/compiler/aot/dispatch_table_generator.cc`
  - computes final dispatch table offsets and table layout.
- `vm/compiler/aot/aot_call_specializer.cc`
  - replaces AOT instance calls with dispatch table calls.
- `vm/compiler/backend/flow_graph_compiler_*.cc`
  - emits static calls, instance calls, and dispatch table calls.

Still missing for typed direct calls:

- Layout, signature, type, selector, and export metadata in the ABI manifest.
- ABI import reference encoding in the snapshot. Modules still deserialize
  their own declarations using VM-isolate base objects and shifted private CIDs.
- Shared ABI `Library`, `Class`, `Function`, `Field`, `Type`, and
  `TypeArguments` identity.
- Stable semantic selector keys and manifest-defined dispatch offsets.
- Dispatch table contributions keyed by selector id/key. The current merge
  copies serialized rows using raw offsets.
- Load-time class layout, member signature, field layout, selector, override,
  and generic type validation beyond header/hash/count checks.
- Module static calls to host ABI functions through linked imports.
- Generated host link-slot stubs for module exports.
- Type-test, cast, generic type, and inherited-field guarantees for shared ABI
  declarations.

## Core Invariants

Typed direct calls only work if the following invariants hold.

### Shared Declaration Identity

For any declaration in the shared ABI:

```text
same source ABI declaration == same runtime VM object
```

Examples:

```text
package:api/foo.dart class Foo
  -> exactly one Class object
  -> exactly one CID
  -> exactly one canonical Type object shape

package:api/foo.dart Foo.bar
  -> exactly one Function object for the ABI declaration, if host-owned
  -> exactly one selector key for virtual dispatch
```

Modules must not deserialize a duplicate `Class` object for a shared ABI class.
They must deserialize an import reference that resolves to the host class.

### Stable Class IDs

Shared ABI classes must have stable CIDs. Module-private classes can receive
fresh CIDs at load time, but any code that embeds or indexes by ABI CID must
observe the host CID.

### Stable Selector Identity

Shared ABI selectors must have stable selector ids and dispatch table offsets.
Two independently compiled units cannot freely assign different offsets to the
same typed interface call.

### Conservative World Assumptions

The host compiler must assume that ABI classes can receive module subclasses and
that ABI members can receive module overrides when the ABI allows it. Otherwise
host AOT could devirtualize or inline in a way that becomes invalid after module
load.

### ABI Compatibility Is Load-Time Checked

The loader must reject a module if it was compiled against an incompatible ABI:

- SDK or target mismatch;
- manifest version mismatch;
- class layout mismatch;
- member signature mismatch;
- selector id or offset mismatch;
- field offset mismatch;
- incompatible nullability, type parameter, or bounds metadata.

## ABI Manifest

The ABI manifest is produced by the host compilation and consumed by module
compilation. It should be serialized in a deterministic binary form for the VM
and optionally in a human-readable form for debugging.

The manifest is not a general package summary. It is the binary contract needed
for AOT code generation and runtime linking.

### Header

```text
magic
manifest_version
sdk_hash
target_arch
target_os
compressed_pointers
product_mode
sound_null_safety
compiler_flags_hash
abi_hash
```

The `abi_hash` is computed from all semantic ABI entries, not from incidental
serialization order.

### Libraries

Each shared ABI library entry:

```text
library_id
canonical_uri
source_digest or kernel_digest
flags
```

The canonical URI must be the only identity key used by both host and module.
Do not compare raw `String` object identity for ABI matching.

### Classes

Each ABI class entry:

```text
class_id_in_manifest
canonical_key:
  library_uri
  class_name
stable_cid
type_parameter_count
type_parameter_bounds_hash
superclass_key
interface_keys
mixin_key
instance_size
host_instance_field_count
field_layout_hash
flags:
  abstract
  enum
  final
  base/interface/final/sealed modifiers if represented
  externally_extendable
  has_module_subclasses
```

The `stable_cid` is the runtime CID assigned in the host snapshot. Module code
must use this CID when generating checks against ABI classes.

For ABI classes that module code may extend, the host must mark the class as
dynamically extendable. Existing related code:

- `dyn-module:extendable`
- `dyn-module:implicitly-extendable`
- `Class::set_has_dynamically_extendable_subtypes`
- `IsolateGroup::set_has_dynamically_extendable_classes`

### Fields

Each ABI field entry:

```text
field_id_in_manifest
canonical_key:
  library_uri
  enclosing_class_key or top-level
  field_name
is_static
is_final
is_late
is_covariant
guarded_cid if relevant
instance_offset for instance fields
static_field_id if host-owned static
declared_type_hash
```

Instance field offsets must be stable for all code that accesses ABI fields
directly.

Static fields need a policy:

- Host-owned ABI statics are stored in the host object store/static field table.
- Module-private statics are stored in the module static field table.
- Module code that references an ABI static must link to the host field.

### Functions and Procedures

Each ABI member entry:

```text
member_id_in_manifest
canonical_key:
  library_uri
  enclosing_class_key or top-level
  name
  kind:
    method
    getter
    setter
    constructor
    factory
    top_level_function
    top_level_getter
    top_level_setter
is_static
is_abstract
is_external
is_overridable_from_module
is_callable_from_module
signature_hash
entry_point_kind
requires_arguments_descriptor
recognized_kind if ABI-relevant
```

If the function body is host-owned, module static calls to it can link directly
to the host `Function`. If the function is a module export, host calls need a
load-time or first-call link slot because the target does not exist when the
host was compiled.

### Selectors

Each ABI selector entry:

```text
selector_id
semantic_key:
  name
  call_kind:
    method
    getter
    setter
    tearoff
  positional_count
  named_names
  type_args_len
dispatch_table_offset
called_on_null
on_null_interface
requires_args_descriptor
```

The selector offset is the critical value for GDT calls. A host call and a
module call to the same ABI interface member must use the same offset.

### Exports and Imports

The manifest should distinguish:

```text
host_exports:
  ABI functions/classes/statics callable by modules

module_imports:
  ABI declarations a module is allowed to reference

module_exports:
  declarations a module may provide to the host

implementation_slots:
  ABI methods module classes may override
```

This distinction lets tree shaking keep only required entry points while still
allowing future module implementations.

## Compiler Pipeline Changes

### Host Compilation

Host compilation becomes responsible for producing the ABI manifest and
compiling with module-aware assumptions.

Required changes:

1. Read user ABI specification.
2. Annotate the kernel component with dynamic-module pragmas.
3. Run TFA with external extension and override assumptions.
4. Assign stable CIDs to ABI classes.
5. Assign stable selector ids and dispatch table offsets to ABI selectors.
6. Retain ABI callable members and host exports.
7. Emit the ABI manifest into:
   - standalone file for module compilation;
   - host snapshot metadata for load-time validation.

Existing useful hooks:

- `pkg/vm/lib/transformations/dynamic_interface_annotator.dart`
- `pkg/vm/lib/transformations/pragma.dart`
- `vm/kernel_loader.cc` pragma handling
- `Precompiler::AddRoots`
- `TableSelectorAssigner`
- `DispatchTableGenerator`

### Module Compilation

Module compilation consumes the host ABI manifest.

Required changes:

1. Read ABI manifest before TFA and selector assignment.
2. Treat ABI libraries/classes/members as imported, not module-owned.
3. Preserve ABI CIDs and selector offsets from the manifest.
4. Generate code that references ABI objects through import references.
5. Emit module-private classes with relocatable or load-assigned CIDs.
6. Emit module dispatch metadata keyed by selector id, not raw local offset.
7. Emit module export metadata for host-to-module static/direct calls.
8. Emit an ABI hash in the module snapshot.

The module compiler should fail if it sees a static type from a library that is
neither:

- in the ABI manifest;
- private to the module;
- explicitly bundled as a private implementation dependency.

This prevents accidental duplicate typed dependencies.

### TFA and CHA Rules

For every ABI class that modules may extend:

- Do not assume the host knows all subclasses.
- Do not convert virtual calls to direct calls solely because the host program
  has a single implementation.
- Keep dispatch table entries for externally visible methods.
- Preserve type-test paths that can handle module subclass CIDs.

For every ABI member that modules may override:

- Do not inline based on "not overridden" unless the ABI forbids module
  overrides.
- Mark dependent optimized code assumptions as invalid at compile time rather
  than relying on runtime deoptimization.

In AOT there is no speculative recovery path comparable to JIT deoptimization.
The host must be compiled conservatively for every ABI feature that modules can
change.

## Snapshot Format Changes

The module snapshot needs a link section. Conceptually:

```text
module_snapshot:
  normal snapshot header
  ABI hash
  ABI import table
  module export table
  class-id relocation table
  selector relocation table
  dispatch table contribution
  static field contribution
  object graph
  code image
```

Currently implemented subset:

- `kDartModuleAbiData` is emitted beside module snapshot data/instructions.
- The ABI sidecar contains header compatibility data, an optional manifest hash,
  and fixed runtime-id counts.
- The normal program roots also serialize a manifest hash and selector count.
- Tooling can produce and consume a deterministic JSON manifest envelope. The
  current semantic payload can include dynamic-interface-derived libraries,
  classes, and members. It does not yet include layout, signature, type,
  selector, or export records.
- Module export data is built after deserialization from surviving top-level
  fields/functions/getters and stored in `LoadedModule::exports`.
- There is no ABI import table, relocation table, semantic selector table, or
  serialized signature/layout manifest yet.

### ABI Import Table

Entries reference host objects by manifest id:

```text
kind:
  library
  class
  type
  field
  function
  selector
manifest_id
expected_hash
```

During deserialization, references to ABI imports are resolved to existing host
objects instead of allocating new objects.

### Module Export Table

Entries expose module-owned declarations to the host:

```text
export_name
kind
library_uri
class_name optional
member_name
signature_hash
function_ref or field_ref
```

Host-side APIs and link stubs use this table to resolve module exports.

### CID Relocations

If module-private class CIDs are assigned at load time, any compiled code or
metadata that embeds a private CID must be relocatable.

Preferred approach:

- Keep ABI CIDs fixed.
- Assign module-private CIDs in a contiguous range at load.
- Record all private-CID embedded constants in a relocation table.
- Patch object pools or metadata arrays during load.

Avoid patching instruction bytes where possible. Prefer object-pool constants or
metadata tables.

### Selector Relocations

For GDT calls, a compiled call sequence embeds or references a selector offset.
For ABI selectors, the offset is fixed by the manifest. For module-private
selectors, offsets can be assigned at load.

The module snapshot must therefore emit dispatch information as:

```text
selector_key or selector_manifest_id
local_selector_offset if private
row entries by module class local cid
```

The loader maps this to global dispatch table offsets.

## Runtime Loader Design

Module loading should be split into explicit phases.

### Phase 1: Open and Validate

In `Module_load`:

1. Open the dynamic library.
2. Resolve snapshot data and instructions symbols.
3. Verify `Snapshot::kFullAOTModule`.
4. Read module ABI header.
5. Compare module ABI hash to host ABI hash.
6. Reject incompatible SDK, architecture, null-safety, compiler flags.

### Phase 2: Reserve Runtime IDs

Under `program_lock` write:

1. Allocate `LoadedModule` and reserve a module id.
2. Reserve class table slots for module-private classes.
3. Reserve private selector ids and dispatch table capacity for module-private
   selectors/classes.
4. Prepare import/export/link tables.

### Phase 3: Deserialize With ABI Imports

`ModuleDeserializationRoots` should become ABI-aware:

- When a ref is a VM-isolate object, resolve as today.
- When a ref is an ABI import, resolve to host object from the manifest.
- When a ref is module-private, allocate normally.

This replaces the current "use VM isolate base objects plus shifted CIDs" model
for shared ABI declarations.

### Phase 4: Apply Relocations

Apply:

- private CID relocations;
- private selector offset relocations;
- static field table bindings;
- function/code entry bindings;
- type-test metadata bindings.

### Phase 5: Merge Dispatch Table

Build a new dispatch table from the host table plus the module contribution.

Pseudo-code:

```cpp
for each existing host table entry:
  copy to new table

for each module dispatch row:
  global_selector_offset = ResolveSelectorOffset(row.selector)
  for each module class entry:
    global_cid = module.base_cid + entry.local_cid_delta
    new_table[global_selector_offset + global_cid] = entry.target_entry
```

Do not copy module rows by raw serialized offset. Raw offsets are only valid
inside the compilation that produced them.

### Phase 6: Publish

Publish the module atomically:

- install merged dispatch table on `IsolateGroup`;
- add `LoadedModule` to `loaded_modules_`;
- publish Dart-level `Module` object;
- make export table visible;
- keep dynamic library handle open.

All object references stored in `LoadedModule` must be visited by
`LoadedModule::VisitObjectPointers`.

## Dispatch Table Architecture

Current AOT GDT calls look like:

```text
load receiver cid
call [dispatch_table + cid * wordSize + selector_offset * wordSize]
```

This only works across modules if `selector_offset` is globally meaningful.

### Global Selector Table

Add a global selector table to the isolate group:

```cpp
struct GlobalSelector {
  SelectorKey key;
  intptr_t selector_id;
  intptr_t dispatch_table_offset;
  bool requires_args_descriptor;
  bool on_null_interface;
};
```

The host precompiler initializes it from the ABI manifest. The module loader
extends it for module-private selectors.

### Selector Key

A selector key must encode the invocation shape, not just the name:

```text
name
kind: method/getter/setter/tearoff
positional_count
named_argument_names
type_args_len
```

If named argument order is canonicalized by arguments descriptors, the key must
use canonical sorted names.

### Table Growth

Loading a module can increase:

- number of classes;
- number of selectors;
- number of table entries.

The loader should create a new `DispatchTable`, fill it, then publish it. Do
not mutate the existing table in place unless all mutators are stopped and the
table has enough capacity. The current `IsolateGroup::set_dispatch_table` style
already supports pointer replacement.

## Static and Direct Calls

There are three static/direct-call cases.

### Module Calls Host ABI Function

The module compiler emits an import reference to a host `Function`.

At load:

```text
module function ref -> host Function object
module call target  -> host code entry
```

The normal optimized static call path can be used after linking.

### Host Calls Module Export

The host cannot have a direct `Function` object for a module export at host
compile time. Use a link-slot call.

Conceptual call sequence:

```text
load export slot
if unresolved:
  call ModuleExportResolveStub(slot)
call resolved target entry
```

After first resolution:

```text
slot.target_function = module Function
slot.target_code = module Code
slot.entry_point = module entry
```

The host API can expose this either explicitly:

```dart
final f = module.lookupFunction<T>('exportName');
f(...);
```

or through generated stubs:

```dart
external int pluginAdd(int a, int b);
```

Generated stubs are preferable for ergonomic typed APIs, but the first
implementation can use explicit lookup.

### Module Calls Module-Private Function

This remains a normal module-local static call.

## Instance Calls

Typed interface calls should use GDT.

Example:

```dart
abstract interface class Processor {
  int process(int value);
}
```

Host:

```dart
int run(Processor p) => p.process(1);
```

Module:

```dart
class PluginProcessor implements Processor {
  int process(int value) => value + 1;
}
```

Requirements:

- `Processor` is an ABI class.
- `process` has an ABI selector with stable dispatch table offset.
- Host `run` compiles to a GDT call using the stable offset.
- Module load adds a dispatch table entry:

```text
dispatch_table[process.offset + PluginProcessor.cid] = PluginProcessor.process
```

Then host code can call module code without reflection or dynamic lookup.

## Type Tests and Casts

Type tests against ABI classes use shared host class identity.

```dart
if (x is Processor) ...
```

When `x` is a module object, its CID is module-private, but its class metadata
must list the ABI interface `Processor` using the host `Class` object.

The subtype test cache and type testing stubs must see:

```text
module class -> implements host ABI class
```

Required loader work:

- Resolve interface/supertype refs in module classes to host ABI classes.
- Update class table metadata for module classes after deserialization.
- Ensure subtype caches can be invalidated or safely extended when module
  classes are added.

Host AOT must not compile a type test as impossible merely because no host class
implements the ABI type.

## Generics

Generic ABI types require canonical type identity.

Examples:

```dart
List<Processor>
Map<String, Processor>
Future<Processor>
```

Requirements:

- ABI `Type` and `TypeArguments` objects must canonicalize using host ABI class
  objects.
- Module deserialization must resolve ABI type refs to host type refs.
- Bounds checks in module code must use host ABI class IDs and type-testing
  metadata.
- Generic function signatures in the manifest must include type parameter count,
  bounds hash, default types, and variance where relevant.

If type canonicalization is incomplete, restrict the first typed-direct-call
version to non-generic ABI signatures except SDK generics that are already
host-owned.

## Fields and Object Layout

Typed direct field access is only safe for ABI fields with stable layout.

Rules:

- ABI instance fields have host-defined offsets.
- Module subclasses append fields after inherited ABI fields.
- Module code must use host ABI field offsets for inherited fields.
- Host code may directly load ABI fields from module subclass instances because
  inherited layout is stable.
- Module-private fields are not visible to host typed code.

For static fields:

- ABI statics are host-owned.
- Module references to ABI statics link to host storage.
- Module-private statics remain in `LoadedModule` static field tables.

## Constructors

Constructors have two cases.

### ABI Constructors

If host and module both statically reference a constructor, the constructed
class must be an ABI class and the constructor belongs to the ABI manifest.

### Module Export Constructors

If a module exports a constructor for a module-private implementation class, the
host should call it through an export link slot or generated factory wrapper.
The return static type should be an ABI supertype or `Object`.

Example:

```dart
// ABI
abstract interface class Processor {
  int process(int value);
}

// Module export
@pragma('dyn-module:entry-point')
Processor createProcessor() => PluginProcessor();
```

This avoids making `PluginProcessor` itself host-nameable.

## Object Ownership

Objects allocated by module code live in the same isolate group heap as host
objects. Ownership is therefore not about memory ownership. It is about metadata
identity:

- ABI object metadata is host-owned.
- Module-private metadata is module-owned.
- Instance objects can freely reference each other if their classes are valid in
  the shared class table.

The GC must visit all module metadata roots, import tables, export tables,
static field tables, and any link slots.

## API Surface

Low-level explicit APIs are useful even if generated typed stubs are added
later.

The current public API in `sdk/lib/module/module.dart` is:

```dart
class ModuleSource {
  final String path;
  ModuleSource(this.path);
}

class Module {
  external factory Module.load(ModuleSource source);
  external T getValue<T>(String valueName);
  external T lookupFunction<T>(String exportName);
  external T invokeMethod<T>(
    String name, {
    List<Object?> positionalArgs = const [],
    List<Object?> optionalArgs = const [],
    Map<String, Object?> namedArgs = const {},
  });
  external T invokeStaticMethod<T>(
    String className,
    String name, {
    List<Object?> positionalArgs = const [],
    List<Object?> optionalArgs = const [],
    Map<String, Object?> namedArgs = const {},
  });
  external T invokeConstructor<T>({
    String? className,
    String? constructorName,
    List<Object?> positionalArgs = const [],
    List<Object?> optionalArgs = const [],
    Map<String, Object?> namedArgs = const {},
  });
}
```

The current VM patch casts the result of the native function lookup to `T`.
Full export-signature validation is still future work.

### Load With ABI Target Shape

```dart
class ModuleSource {
  final String path;
  final String? abiPath;
  const ModuleSource(this.path, {this.abiPath});
}

class Module {
  external factory Module.load(ModuleSource source);
}
```

The ABI may be embedded in the host snapshot, so `abiPath` is optional.

### Lookup Exported Function

```dart
extension ModuleTypedLookup on Module {
  external T lookupFunction<T>(String exportName);
}
```

The VM validates that `T` is compatible with the export signature if enough type
metadata is available. Today `Module.lookupFunction<T>` returns an implicit
static closure for a retained top-level regular function and relies on the Dart
cast in the VM patch.

### Generated API Facade

A package tool can generate:

```dart
final class PluginApi {
  final Module _module;

  late final int Function(int, int) add =
      _module.lookupFunction<int Function(int, int)>('add');
}
```

Later, the compiler can recognize generated stubs and lower them to link-slot
calls.

## Native Runtime Structures

Current `LoadedModule` ABI-aware state:

```cpp
struct LoadedModule {
  intptr_t id;
  void* dl_handle;
  const uint8_t* isolate_data;
  const uint8_t* isolate_instructions;
  const uint8_t* abi_data;
  intptr_t abi_data_size;
  ModuleAbiHeader abi_header;
  ModuleAbiRuntimeIds abi_runtime_ids;

  ObjectStore* object_store;

  ObjectPtr* initial_field_values;
  intptr_t initial_field_count;
  ObjectPtr* shared_initial_field_values;
  intptr_t shared_initial_field_count;

  ObjectPtr* classes;
  intptr_t class_object_count;

  DispatchTable* dispatch_table;

  intptr_t base_class_id;
  intptr_t class_count;
  intptr_t base_selector_id;
  intptr_t selector_count;
  intptr_t dispatch_table_entry_count;

  ArrayPtr abi_imports;
  ArrayPtr exports;
  ArrayPtr export_link_slots;
  ArrayPtr selector_bindings;
  ArrayPtr class_id_bindings;
};
```

The exact representation can use VM arrays first. C++ structs can be introduced
after the design stabilizes.

Current `IsolateGroup` module state:

```cpp
std::unique_ptr<DispatchTable> dispatch_table_;
MallocGrowableArray<LoadedModule*> loaded_modules_;
ArrayPtr module_abi_manifest_;
uint64_t module_abi_manifest_hash_;
uint32_t module_abi_selector_count_;
intptr_t next_module_selector_id_;
```

Future typed-direct-call support still needs:

```cpp
AbiManifest* abi_manifest;
GlobalSelectorTable* selector_table;
ExportRegistry* module_export_registry;
```

The host snapshot should initialize these. Module loading extends them.

## Deserialization Details

### Import Reference Encoding

Introduce a snapshot reference kind for ABI imports. During serialization, if an
object is in the ABI import set, write:

```text
RefKind::kAbiImport
manifest_object_kind
manifest_object_id
```

During deserialization:

```cpp
ObjectPtr ResolveAbiImport(kind, id) {
  return isolate_group->abi_manifest()->ObjectFor(kind, id);
}
```

This avoids allocating duplicate ABI `Library`, `Class`, `Function`, `Field`,
`Type`, and `TypeArguments` objects.

### Class Table Installation

For module-private classes:

1. Read class object.
2. Apply CID offset or assign reserved CID.
3. Resolve supertype/interface refs.
4. Install in shared class table.
5. Copy class sizes and allocation metadata.

For ABI classes:

1. Do not deserialize a new class.
2. Resolve to host class.
3. Validate layout hash and metadata hash.

### Code and Object Pool Linking

Module code object pools may contain:

- ABI function refs;
- ABI class refs;
- ABI type refs;
- selector offsets;
- CIDs;
- static field refs.

All must be patched or resolved before the code is callable.

For AOT snapshots compiled into shared objects, prefer object-pool patching over
instruction patching. If an instruction immediate must be patched, the snapshot
must mark the page writable temporarily and flush instruction cache, which is
more fragile.

## Failure Modes

The loader must produce clear errors for:

- Missing ABI manifest in host.
- Module ABI hash mismatch.
- ABI library not found in host.
- ABI class layout mismatch.
- ABI selector offset mismatch.
- Module override of non-overridable member.
- Module subclass of non-extendable ABI class.
- Signature mismatch for imported host function.
- Signature mismatch for exported module function.
- Unsupported generic ABI signature in early implementation stages.

Fail before publishing the module.

## Concurrency and Safepoints

Module loading mutates shared isolate-group structures:

- class table;
- dispatch table;
- selector table;
- loaded module list;
- subtype/type-test metadata;
- export registry.

Hold `program_lock` write for structural mutation. Publish by replacing pointers
after fully constructing new tables.

If existing mutators can execute while the dispatch table pointer changes, the
new table must remain permanently valid. Old tables must not be freed until no
code can read them. The current `unique_ptr` replacement model may need review
if loads can happen concurrently with Dart execution. A conservative first
implementation can load modules only at safepoints with mutators stopped.

## Concrete Implementation Map

This section maps the architecture to likely implementation work in the current
tree.

### Front End and Kernel Metadata

Likely files:

- `pkg/vm/lib/kernel_front_end.dart`
- `pkg/vm/lib/transformations/dynamic_interface_annotator.dart`
- `pkg/vm/lib/transformations/pragma.dart`
- `pkg/vm/lib/transformations/type_flow/transformer.dart`
- `pkg/vm/lib/transformations/type_flow/table_selector_assigner.dart`
- `pkg/vm/lib/metadata/*`

Work items:

1. Add a typed-module ABI input flag for module compilation.
2. Add a typed-module ABI output flag for host compilation.
3. Represent ABI libraries, classes, members, fields, selectors, and exports in
   a deterministic metadata model.
4. Extend `TableSelectorAssigner` so it can:
   - reserve manifest selector ids;
   - assign new private selector ids after the reserved range;
   - report selector keys and call counts for manifest emission.
5. Extend TFA annotations so ABI classes and members receive the correct
   dynamic-module pragmas even when the ABI is external to the current component.
6. Ensure ABI callables are retained as roots.

The current dynamic interface annotator already discovers callable, extendable,
and overridable sets. Typed modules should reuse that vocabulary, but the output
must become a binary ABI contract, not only pragmas in the component.

### VM Kernel Loader

Likely files:

- `vm/kernel_loader.cc`
- `vm/kernel_loader.h`
- `vm/compiler/frontend/kernel_translation_helper.cc`
- `vm/compiler/frontend/kernel_translation_helper.h`
- `vm/kernel.cc`
- `vm/kernel.h`

Work items:

1. Read new ABI metadata from Kernel.
2. Preserve ABI class and member attributes on VM objects.
3. Expose ABI selector metadata to the precompiler.
4. Mark classes as dynamically extendable when the ABI allows module
   subclasses.
5. Mark functions as dynamically overridable when the ABI allows module
   overrides.
6. Record stable manifest ids on loaded ABI objects if needed for fast lookup.

The loader should not rely on URL string object identity. ABI lookup must use a
canonical key such as `(library_uri, class_name, member_name, member_kind)`.

### Precompiler

Likely files:

- `vm/compiler/aot/precompiler.cc`
- `vm/compiler/aot/precompiler.h`
- `vm/compiler/aot/aot_call_specializer.cc`
- `vm/compiler/compiler_pass.cc`
- `vm/compiler/aot/dispatch_table_generator.cc`
- `vm/compiler/aot/dispatch_table_generator.h`

Host work items:

1. Keep ABI roots alive.
2. Force conservative CHA/TFA behavior for extendable and overridable ABI
   declarations.
3. Emit the ABI manifest after class IDs, selector IDs, selector offsets, field
   offsets, and signatures are finalized.
4. Include the ABI manifest or ABI hash in the host snapshot.

Module work items:

1. Load the host ABI manifest before TFA and selector assignment.
2. Treat ABI declarations as imports.
3. Make the precompiler produce import records instead of serializing duplicate
   ABI objects.
4. Emit module dispatch contributions by selector manifest id.
5. Emit export metadata for module declarations callable by the host.

### Dispatch Table Generator

Likely files:

- `vm/compiler/aot/dispatch_table_generator.cc`
- `vm/compiler/aot/dispatch_table_generator.h`
- `vm/dispatch_table.h`
- `vm/app_snapshot.cc`

Work items:

1. Split selector identity from selector offset.
2. Add a `SelectorKey` representation that can be serialized into the ABI
   manifest.
3. Reserve manifest offsets before fitting module-private selector rows.
4. Teach the module serializer to write rows as `(selector_id, cid_delta,
   target_code)` rather than relying only on local row offsets.
5. Teach `ReadModuleDispatchTable` to merge by selector id or selector key.

The existing row-fitting algorithm can remain for private selectors. ABI
selectors are fixed slots.

### Snapshot Serializer and Deserializer

Likely files:

- `vm/app_snapshot.cc`
- `vm/app_snapshot.h`
- `vm/snapshot.h`
- `vm/image_snapshot.h`
- `vm/object_store.h`
- `vm/raw_object.h`

Work items:

1. Add an ABI import reference encoding.
2. Add a module link section:
   - ABI hash;
   - ABI import table;
   - module export table;
   - selector binding table;
   - class ID relocation table;
   - dispatch table contribution.
3. During module deserialization, resolve ABI import refs to host objects.
4. During module deserialization, allocate and install module-private classes in
   fresh class table slots. The current implementation does this with
   `cid_offset_` plus reserved class table slots.
5. Apply object-pool and metadata relocations before publishing code.
6. Extend ABI root recording as new link tables are added. Current module
   object-store roots, static field tables, class lists, and ABI link-table
   arrays are recorded and visited by `LoadedModule::VisitObjectPointers`.

The current `cid_offset_` approach is useful for private module classes, but it
is not sufficient for shared ABI classes because those must not be shifted
duplicates. ABI classes should resolve to existing host class objects.

### Runtime Module API

Likely files:

- `lib/module.cc`
- `sdk/lib/module/module.dart`
- `sdk/lib/_internal/vm/lib/module_patch.dart`
- `vm/bootstrap_natives.h`
- `vm/bootstrap_natives.cc`

Current status and work items:

1. `Module.lookupFunction<T>(String exportName)` exists.
2. Native lookup uses `LoadedModule::exports` first and falls back to scanning
   module top-level functions.
3. The current implementation returns an implicit closure for retained
   top-level regular functions.
4. Missing export diagnostics exist, but full ABI mismatch diagnostics remain
   tied to the module ABI header/hash checks.
5. Requested function type validation against export signature metadata is still
   missing.
6. VM-generated link-slot closures/stubs are still missing.
7. Existing `Module.invoke*` APIs remain as reflective fallback and debugging
   tools.

The explicit `lookupFunction` API is the fastest path to typed host-to-module
calls because it can return a normal closure before the compiler grows generated
link-slot stubs.

### Runtime Link Slots

Likely files:

- `vm/object.h`
- `vm/raw_object.h`
- `vm/object.cc`
- `vm/stub_code_list.h`
- `vm/compiler/stub_code_compiler_*.cc`
- `vm/runtime_entry.cc`

Potential object:

```cpp
class ModuleExportLink : public Object {
  ModulePtr module;
  StringPtr export_name;
  FunctionPtr target_function;
  CodePtr target_code;
  uword entry_point;
  ArrayPtr arguments_descriptor;
};
```

First implementation can avoid a new heap object and use a small VM array:

```text
[module, export_name, target_function, target_code, entry_point, args_desc]
```

Work items:

1. Add a runtime entry that resolves an export link slot.
2. Verify module is loaded and export signature matches.
3. Fill the target fields.
4. Return or tail-call the target.
5. Optionally patch future calls to skip the resolver.

### Type Testing and Subtyping

Likely files:

- `vm/compiler/type_testing_stubs.*`
- `vm/compiler/backend/flow_graph_compiler_*.cc`
- `vm/object.cc`
- `vm/class_finalizer.cc`
- `vm/isolate_reload.cc` if reload interactions matter later.

Work items:

1. Ensure module-private classes can list host ABI classes as supertypes and
   interfaces.
2. Ensure subtype caches include module classes.
3. Avoid host AOT reductions that assume no future ABI implementors.
4. Add tests for type checks compiled before module load and executed after
   module load.

### GC and Object Visiting

Likely files:

- `vm/isolate.cc`
- `vm/isolate.h`
- `vm/object_store.h`

Current status and work items:

1. `LoadedModule::VisitObjectPointers` visits the module object store, static
   field tables, class list, ABI import/export/link arrays, selector bindings,
   and class-id bindings.
2. Continue to visit any future host-owned ABI objects referenced from
   module-owned tables.
3. The dynamic library handle is intentionally kept open for module code
   lifetime; a real unloading policy is still future work.

### Build Tool Flow

The low-level flow is through `gen_snapshot`'s `app-aot-module` snapshot kind
and `FLAG_module_abi_manifest_hash`. The Stage 1 user-facing flow is:

The build flow should become explicit:

```text
host:
  dart compile exe --emit-module-abi=app.abi.json app.dart

module:
  dart compile module --module-abi=app.abi.json -o plugin.so plugin.dart
```

The exact flags can differ, but the dependency direction should be clear:

```text
host snapshot -> ABI manifest -> module snapshot
```

The module must not be compiled against a guessed ABI.

## Testing Plan

### Unit Tests

- ABI manifest serialization is deterministic.
- Selector key equality handles named argument order.
- ABI import resolution returns host objects.
- Duplicate ABI class deserialization is rejected or redirected.
- Dispatch table merge uses semantic selector keys, not local offsets.

### VM Tests

Host to module:

- Host calls ABI interface method implemented by module class.
- Host casts module object to ABI interface.
- Host reads ABI inherited field from module subclass.
- Host calls module exported factory returning ABI interface.

Module to host:

- Module calls host top-level ABI function.
- Module calls host static method.
- Module extends host ABI base class and calls `super`.
- Module implements host ABI interface.

Generics:

- Module returns `List<AbiInterface>`.
- Host passes `Map<String, AbiInterface>` to module.
- Generic method with ABI bound, if supported.

Negative tests:

- ABI hash mismatch.
- Selector mismatch.
- Layout mismatch.
- Override not allowed.
- Extension not allowed.
- Missing export.
- Wrong function type in `lookupFunction<T>`.

### Stress Tests

- Load multiple modules implementing the same ABI interface.
- Load modules in different orders.
- Repeated GDT calls before and after module load.
- GC during and after module load.
- Exception thrown across host/module boundary.
- Stack traces across host/module boundary.

## Implementation Stages

Current progress relative to these stages:

- Stage 0 is implemented for the current explicit API surface: `Module.invoke*`,
  `getValue`, and `lookupFunction` exist, no constructor debug prints remain,
  and `tests/lib/module/module_loads_test.dart` documents the current behavior
  and limitations.
- Stage 1 is implemented as infrastructure: ABI header/hash/runtime-id
  scaffolding, load-time compatibility checks, deterministic manifest envelope,
  host `--emit-module-abi`, module `--module-abi` plumbing, and semantic
  manifest entries derived from the detailed dynamic-interface dump. The
  manifest still lacks layout/signature/type records and selector metadata.
- Stage 3 has early selector-count plumbing and module-private selector id
  reservation, but not stable selector keys, manifest offsets, or merge-by-key.
- Stage 5 has the explicit `Module.lookupFunction<T>` closure path and a flat
  export table, but no signature validation, generated stubs, or link slots.

### Stage 0: Document Current Semantics

- Keep existing `Module.invoke*`.
- Add tests that describe the current limitations.
- Remove debug prints from current constructor path before broad testing.

### Stage 1: ABI Manifest Infrastructure

- Add manifest model in `pkg/vm`.
- Emit manifest from host precompiler.
- Consume manifest in module precompiler.
- Add load-time ABI hash validation.

No typed direct calls yet.

### Stage 2: Shared ABI Classes and Types

- Serialize ABI imports instead of duplicate ABI objects.
- Resolve ABI classes/libraries/functions to host objects during module load.
- Validate class layouts and signatures.
- Make simple `is` and `as` work for ABI interfaces.

### Stage 3: Stable Selectors and GDT Merge

- Seed `TableSelectorAssigner` with manifest selector ids.
- Reserve manifest dispatch table offsets in `DispatchTableGenerator`.
- Emit module dispatch table contributions keyed by selector id.
- Merge dispatch table by selector key.

At this point host typed interface calls can dispatch to module classes.

### Stage 4: Static Host Imports

- Let module code statically call host ABI functions.
- Link module object pools to host `Function` and `Code`.
- Validate argument descriptors and signatures.

### Stage 5: Module Exports to Host

- Add module export table.
- Add `Module.lookupFunction<T>`.
- Return closures or generated link-slot wrappers.
- Add first-call patching for direct export calls if needed.

### Stage 6: Generics and Advanced Type Tests

- Expand manifest type metadata.
- Canonicalize generic ABI types across module load.
- Validate bounds and variance.
- Add generic ABI tests.

### Stage 7: Multiple Modules and Unloading Policy

- Support multiple modules contributing to the same ABI selectors.
- Define whether modules can unload. A simple first policy is no unload.
- If unload is required, add code lifetime tracking, dispatch table tombstones,
  and safe reclamation.

## Key Design Decision

The essential design decision is that typed direct calls require a shared ABI
universe. The loader can append module-private implementation classes and code,
but it must not duplicate declarations that typed code on both sides treats as
the same API.

This is the difference between:

```text
reflection-style module calls:
  names + runtime lookup + duplicated worlds

typed direct module calls:
  shared ABI identity + stable CIDs + stable selectors + merged dispatch table
```

The latter is more work, but it lets the VM keep normal AOT call machinery
instead of adding a parallel reflective invocation path.

// Copyright (c) 2026, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "../vm/exceptions.h"
#include "../vm/object.h"
#include "vm/bootstrap_natives.h"

#include <cstdlib>
#include <memory>
#include "include/dart_api.h"
#include "platform/lockers.h"
#include "platform/utils.h"
#include "vm/app_snapshot.h"
#include "vm/dart_entry.h"
#include "vm/exceptions.h"
#include "vm/flags.h"
#include "vm/growable_array.h"
#include "vm/isolate.h"
#include "vm/module_abi.h"
#include "vm/native_entry.h"
#include "vm/object.h"
#include "vm/object_store.h"
#include "vm/resolver.h"
#include "vm/snapshot.h"
#include "vm/symbols.h"

namespace dart {
// namespace

#if (defined(DART_INCLUDE_SIMULATOR) && !defined(SIMULATOR_FFI)) ||            \
    (defined(DART_PRECOMPILER) && !defined(TESTING))

[[noreturn]] static void ThrowModuleUnsupported() {
  Exceptions::ThrowUnsupportedError("dart:module is not implemented yet.");
  UNREACHABLE();
}

DEFINE_NATIVE_ENTRY(Module_load, 0, 2) {
  ThrowModuleUnsupported();
}

DEFINE_NATIVE_ENTRY(Module_getValue, 0, 2) {
  ThrowModuleUnsupported();
}

DEFINE_NATIVE_ENTRY(Module_lookupFunction, 0, 2) {
  ThrowModuleUnsupported();
}

DEFINE_NATIVE_ENTRY(Module_invokeMethod, 0, 5) {
  ThrowModuleUnsupported();
}

DEFINE_NATIVE_ENTRY(Module_invokeStaticMethod, 0, 6) {
  ThrowModuleUnsupported();
}

DEFINE_NATIVE_ENTRY(Module_invokeConstructor, 0, 6) {
  ThrowModuleUnsupported();
}

#else

constexpr const char* kModuleLibraryUrl = "dart:module";
constexpr const char* kFileModuleSourceClassName = "ModuleSource";
constexpr const char* kFileModuleSourcePathField = "path";

constexpr const char* kModuleClassName = "Module";
constexpr const char* kModuleNativeIdFieldName = "_nativeId";

[[noreturn]] void ThrowModuleSnapshotError(const char* error) {
  const char* msg = OS::SCreate(Thread::Current()->zone(),
                                "Failed to load module snapshot: %s", error);
  Exceptions::ThrowUnsupportedError(msg);
  UNREACHABLE();
}

[[noreturn]] void ThrowModuleSnapshotMallocError(char* error) {
  const char* copied = OS::SCreate(Thread::Current()->zone(), "%s", error);
  free(error);
  ThrowModuleSnapshotError(copied);
}

[[noreturn]] void ThrowModuleAotOnly() {
  Exceptions::ThrowUnsupportedError("dart:module is only supported in AOT.");
  UNREACHABLE();
}

[[noreturn]] void ThrowModuleSourceUnsupported(const char* type,
                                               const char* name) {
  const String& msg = String::Handle(
      String::NewFormatted("Only FileModuleSource is supported at the moment. "
                           "Unsupported source type: %s '%s'",
                           name, type));
  Exceptions::ThrowArgumentError(msg);
  UNREACHABLE();
}

StringPtr ResolveFileModuleSourcePath(Thread* thread, const Instance& source) {
  Zone* zone = thread->zone();
  const String& library_url =
      String::Handle(zone, Symbols::New(thread, kModuleLibraryUrl));

  const Library& lib =
      Library::Handle(zone, Library::LookupLibrary(thread, library_url));
  if (lib.IsNull()) {
    Exceptions::ThrowUnsupportedError("dart:module library is not loaded.");
    UNREACHABLE();
  }

  const String& class_name =
      String::Handle(zone, Symbols::New(thread, kFileModuleSourceClassName));
  const Class& file_source_class =
      Class::Handle(zone, lib.LookupClassAllowPrivate(class_name));
  if (file_source_class.IsNull() || source.clazz() != file_source_class.ptr()) {
    ThrowModuleSourceUnsupported(source.ToCString(), "Class");
  }

  const String& field_name =
      String::Handle(zone, Symbols::New(thread, kFileModuleSourcePathField));
  const Field& path_field = Field::Handle(
      zone, file_source_class.LookupInstanceFieldAllowPrivate(field_name));
  if (path_field.IsNull()) {
    ThrowModuleSourceUnsupported(path_field.ToCString(), "FieldName");
  }

  const Object& field_value = Object::Handle(zone, source.GetField(path_field));
  if (!field_value.IsString()) {
    ThrowModuleSourceUnsupported(field_value.ToCString(), "val");
  }
  return String::Cast(field_value).ptr();
}

DEFINE_NATIVE_ENTRY(Module_load, 0, 2) {
  if (!FLAG_precompiled_mode) {
    ThrowModuleAotOnly();
  }

  GET_NON_NULL_NATIVE_ARGUMENT(Instance, source, arguments->NativeArgAt(1));

  const String& path =
      String::Handle(zone, ResolveFileModuleSourcePath(thread, source));

  char* error = nullptr;
  void* dl_handle =
      Utils::LoadDynamicLibrary(path.ToCString(),
                                /*search_dll_load_dir=*/false, &error);
  if (dl_handle == nullptr) {
    ThrowModuleSnapshotError(error == nullptr ? "dlopen failed" : error);
  }

  const uint8_t* isolate_snapshot_data =
      reinterpret_cast<const uint8_t*>(Utils::ResolveSymbolInDynamicLibrary(
          dl_handle, kIsolateSnapshotDataCSymbol));
  if (isolate_snapshot_data == nullptr) {
    Utils::UnloadDynamicLibrary(dl_handle);
    ThrowModuleSnapshotError("missing isolate snapshot data symbol");
  }

  const uint8_t* isolate_snapshot_instructions =
      reinterpret_cast<const uint8_t*>(Utils::ResolveSymbolInDynamicLibrary(
          dl_handle, kIsolateSnapshotInstructionsCSymbol));
  if (isolate_snapshot_instructions == nullptr) {
    Utils::UnloadDynamicLibrary(dl_handle);
    ThrowModuleSnapshotError("missing isolate snapshot instructions symbol");
  }

  const Snapshot* snapshot = Snapshot::SetupFromBuffer(isolate_snapshot_data);
  if (snapshot == nullptr) {
    Utils::UnloadDynamicLibrary(dl_handle);
    ThrowModuleSnapshotError("invalid snapshot header");
  }
  if (snapshot->kind() != Snapshot::kFullAOTModule) {
    const char* kind_str = Snapshot::KindToCString(snapshot->kind());
    Utils::UnloadDynamicLibrary(dl_handle);
    ThrowModuleSnapshotError(kind_str);
  }

  SnapshotHeaderReader header_reader(snapshot);
  intptr_t snapshot_payload_offset = 0;
  char* snapshot_error = header_reader.VerifyVersionAndFeatures(
      thread->isolate_group(), &snapshot_payload_offset);
  if (snapshot_error != nullptr) {
    Utils::UnloadDynamicLibrary(dl_handle);
    ThrowModuleSnapshotMallocError(snapshot_error);
  }

  const uint8_t* module_abi_data = reinterpret_cast<const uint8_t*>(
      Utils::ResolveSymbolInDynamicLibrary(dl_handle, kModuleAbiDataCSymbol));
  ModuleAbiHeader module_abi_header;
  intptr_t module_abi_data_size = 0;
  if (module_abi_data == nullptr) {
    Utils::UnloadDynamicLibrary(dl_handle);
    ThrowModuleSnapshotError("missing module ABI data symbol");
  }
  const char* abi_error =
      ModuleAbi::ReadHeader(module_abi_data, &module_abi_header);
  if (abi_error != nullptr) {
    Utils::UnloadDynamicLibrary(dl_handle);
    ThrowModuleSnapshotError(abi_error);
  }
  module_abi_data_size = module_abi_header.TotalSize();

  const uint64_t host_abi_hash =
      thread->isolate_group()->module_abi_manifest_hash();
  abi_error = ModuleAbi::ValidateCompatibility(module_abi_header, host_abi_hash,
                                               thread->isolate_group());
  if (abi_error != nullptr) {
    Utils::UnloadDynamicLibrary(dl_handle);
    ThrowModuleSnapshotError(abi_error);
  }

  // Ownership of |loaded| transfers to IsolateGroup inside ReadModuleSnapshot.
  LoadedModule* loaded = new LoadedModule();
  loaded->dl_handle = dl_handle;
  loaded->isolate_data = isolate_snapshot_data;
  loaded->isolate_instructions = isolate_snapshot_instructions;
  loaded->abi_data = module_abi_data;
  loaded->abi_data_size = module_abi_data_size;
  loaded->abi_header = module_abi_header;

  intptr_t module_id = -1;
  {
    FullSnapshotReader reader(snapshot, isolate_snapshot_instructions, thread);
    ApiErrorPtr api_error = reader.ReadModuleSnapshot(loaded, &module_id);
    if (api_error != ApiError::null()) {
      delete loaded;
      const auto& err_obj = ApiError::Handle(zone, api_error);
      const String& msg = String::Handle(zone, err_obj.message());
      Exceptions::ThrowArgumentError(msg);
      UNREACHABLE();
    }
  }
  ASSERT(module_id >= 0);

  // Register Dart-level tracking arrays in the main ObjectStore.
  ObjectStore* object_store = thread->isolate_group()->object_store();

  GrowableObjectArray& modules =
      GrowableObjectArray::Handle(zone, object_store->modules());
  if (modules.IsNull()) {
    modules = GrowableObjectArray::New();
    object_store->set_modules(modules);
  }

  GrowableObjectArray& meta_tables =
      GrowableObjectArray::Handle(zone, object_store->module_meta_tables());
  if (meta_tables.IsNull()) {
    meta_tables = GrowableObjectArray::New();
    object_store->set_module_meta_tables(meta_tables);
  }
  meta_tables.Add(Array::Handle(zone, Array::New(0)));

  GrowableObjectArray& dispatch_tables = GrowableObjectArray::Handle(
      zone, object_store->module_dispatch_table_code_entries());
  if (dispatch_tables.IsNull()) {
    dispatch_tables = GrowableObjectArray::New();
    object_store->set_module_dispatch_table_code_entries(dispatch_tables);
  }
  dispatch_tables.Add(Array::Handle(zone, Array::New(0)));

  GrowableObjectArray& instructions_tables = GrowableObjectArray::Handle(
      zone, object_store->module_instructions_tables());
  if (instructions_tables.IsNull()) {
    instructions_tables = GrowableObjectArray::New();
    object_store->set_module_instructions_tables(instructions_tables);
  }
  instructions_tables.Add(
      GrowableObjectArray::Handle(zone, GrowableObjectArray::New()));

  // Build the Dart Module object. _nativeId is the module's index in the
  // IsolateGroup's loaded_modules_ list, used by Module.invoke* to dispatch.
  const String& library_url =
      String::Handle(zone, Symbols::New(thread, kModuleLibraryUrl));
  const Library& module_lib =
      Library::Handle(zone, Library::LookupLibrary(thread, library_url));
  if (module_lib.IsNull()) {
    ThrowModuleSnapshotError("dart:module library is not loaded");
  }

  const String& class_name =
      String::Handle(zone, Symbols::New(thread, kModuleClassName));
  const Class& module_class =
      Class::Handle(zone, module_lib.LookupClassAllowPrivate(class_name));
  if (module_class.IsNull()) {
    ThrowModuleSnapshotError("Module class not found");
  }

  const Instance& module = Instance::Handle(zone, Instance::New(module_class));
  const String& field_name =
      String::Handle(zone, Symbols::New(thread, kModuleNativeIdFieldName));
  const Field& native_id_field = Field::Handle(
      zone, module_class.LookupInstanceFieldAllowPrivate(field_name));
  if (native_id_field.IsNull()) {
    ThrowModuleSnapshotError("Module._nativeId field not found");
  }
  module.SetField(native_id_field, Smi::Handle(zone, Smi::New(module_id)));
  modules.Add(module);

  return module.ptr();
}

// ---------------------------------------------------------------------------
// Helpers shared by Module_getValue / Module_invoke*
// ---------------------------------------------------------------------------

// Reads _nativeId from a Module instance and returns the LoadedModule.
static LoadedModule* GetLoadedModuleFromObject(Thread* thread,
                                               Zone* zone,
                                               const Instance& module_obj) {
  const Class& cls = Class::Handle(zone, module_obj.clazz());
  const String& id_name =
      String::Handle(zone, Symbols::New(thread, kModuleNativeIdFieldName));
  const Field& id_field =
      Field::Handle(zone, cls.LookupInstanceFieldAllowPrivate(id_name));
  ASSERT(!id_field.IsNull());
  const Object& id_obj = Object::Handle(zone, module_obj.GetField(id_field));
  ASSERT(id_obj.IsSmi());
  const intptr_t id = Smi::Cast(id_obj).Value();
  IsolateGroup* isolate_group = thread->isolate_group();
  LoadedModule* m = nullptr;
  {
    SafepointReadRwLocker ml(thread, isolate_group->program_lock());
    m = isolate_group->GetLoadedModule(id);
  }
  if (m == nullptr || m->object_store == nullptr) {
    Exceptions::ThrowUnsupportedError("Module has no native state");
    UNREACHABLE();
  }
  return m;
}

static ObjectPtr ModuleStaticFieldValue(LoadedModule* module,
                                        const Field& field) {
  ASSERT(field.is_static());
  const intptr_t field_id = field.field_id();
  if (field_id < 0) return Object::sentinel().ptr();

  ObjectPtr* values = field.is_shared() ? module->shared_initial_field_values
                                        : module->initial_field_values;
  const intptr_t count = field.is_shared() ? module->shared_initial_field_count
                                           : module->initial_field_count;
  if (values == nullptr || field_id >= count) {
    return Object::sentinel().ptr();
  }
  return values[field_id];
}

static FieldPtr LookupModuleStaticField(Zone* zone,
                                        const Library& lib,
                                        const String& name) {
  Field& field = Field::Handle(zone, lib.LookupFieldAllowPrivate(name));
  if (!field.IsNull()) return field.ptr();

  lib.EnsureTopLevelClassIsFinalized();
  const Class& toplevel_class = Class::Handle(zone, lib.toplevel_class());
  if (toplevel_class.IsNull()) return Field::null();
  field = toplevel_class.LookupStaticFieldAllowPrivate(name);
  return field.ptr();
}

static FunctionPtr LookupModuleStaticFunction(Zone* zone,
                                              const Library& lib,
                                              const String& name) {
  Function& function =
      Function::Handle(zone, lib.LookupFunctionAllowPrivate(name));
  if (!function.IsNull()) return function.ptr();

  lib.EnsureTopLevelClassIsFinalized();
  const Class& toplevel_class = Class::Handle(zone, lib.toplevel_class());
  if (toplevel_class.IsNull()) return Function::null();
  function = toplevel_class.LookupStaticFunctionAllowPrivate(name);
  return function.ptr();
}

static ObjectPtr LookupModuleExportTarget(LoadedModule* module,
                                          Zone* zone,
                                          const String& name,
                                          ModuleExportKind kind) {
  const Array& exports = Array::Handle(zone, module->exports);
  if (exports.IsNull()) return Object::null();

  String& export_name = String::Handle(zone);
  Object& object = Object::Handle(zone);
  for (intptr_t i = 0; i + ModuleExportTable::kTargetIndex < exports.Length();
       i += ModuleExportTable::kEntryLength) {
    object = exports.At(i + ModuleExportTable::kKindIndex);
    if (!object.IsSmi() ||
        Smi::Cast(object).Value() != static_cast<intptr_t>(kind)) {
      continue;
    }

    object = exports.At(i + ModuleExportTable::kNameIndex);
    if (!object.IsString()) continue;
    export_name ^= object.ptr();
    if (!String::EqualsIgnoringPrivateKey(export_name, name)) continue;

    return exports.At(i + ModuleExportTable::kTargetIndex);
  }
  return Object::null();
}

static FunctionPtr LookupModuleExportFunction(LoadedModule* module,
                                              Zone* zone,
                                              const String& name) {
  const Object& target = Object::Handle(
      zone, LookupModuleExportTarget(module, zone, name,
                                     ModuleExportKind::kFunction));
  if (!target.IsFunction()) return Function::null();
  return Function::Cast(target).ptr();
}

static FunctionPtr LookupModuleTopLevelFunction(LoadedModule* module,
                                                Zone* zone,
                                                const String& name) {
  Function& function =
      Function::Handle(zone, LookupModuleExportFunction(module, zone, name));
  if (!function.IsNull()) return function.ptr();

  const GrowableObjectArray& libs =
      GrowableObjectArray::Handle(zone, module->object_store->libraries());
  if (libs.IsNull()) return Function::null();

  Library& lib = Library::Handle(zone);
  for (intptr_t i = 0; i < libs.Length(); i++) {
    lib ^= libs.At(i);
    function = LookupModuleStaticFunction(zone, lib, name);
    if (!function.IsNull()) return function.ptr();
  }
  return Function::null();
}

static ClassPtr LookupModuleClass(LoadedModule* module,
                                  Zone* zone,
                                  const Library& lib,
                                  const String& name) {
  std::unique_ptr<char> name_cstr{name.ToMallocCString()};
  // Primary: scan the module's own class CID range directly. This is the most
  // reliable path in AOT mode because it does not depend on library dictionary
  // state or URL string comparisons — both of which can be unreliable for
  // module snapshots loaded at runtime.
  {
    ClassTable* class_table = Thread::Current()->isolate_group()->class_table();
    const intptr_t cid_start = module->base_class_id;
    const intptr_t cid_end = cid_start + module->class_count;
    Object& entry = Object::Handle(zone);
    Class& klass = Class::Handle(zone);
    String& klass_name = String::Handle(zone);
    for (intptr_t cid = cid_start; cid < cid_end; cid++) {
      entry = class_table->At(cid);
      if (!entry.IsClass()) continue;
      klass ^= entry.ptr();
      klass_name = klass.Name();

      if (klass_name.Equals(name_cstr.get())) {
        return klass.ptr();
      }
    }
  }

  Class& klass = Class::Handle(zone, lib.LookupClassAllowPrivate(name));
  if (!klass.IsNull()) return klass.ptr();

  String& klass_name = String::Handle(zone);
  ClassDictionaryIterator iterator(lib,
                                   ClassDictionaryIterator::kIteratePrivate);
  while (iterator.HasNext()) {
    klass = iterator.GetNextClass();
    klass_name = klass.Name();
    if (klass_name.Equals(name)) {
      return klass.ptr();
    }
  }

  const GrowableObjectArray& pending_classes = GrowableObjectArray::Handle(
      zone, module->object_store->pending_classes());
  Object& entry = Object::Handle(zone);
  if (!pending_classes.IsNull()) {
    for (intptr_t i = 0; i < pending_classes.Length(); i++) {
      entry = pending_classes.At(i);
      if (!entry.IsClass()) continue;
      klass ^= entry.ptr();
      if (klass.library() != lib.ptr()) continue;
      klass_name = klass.Name();
      if (klass_name.Equals(name)) {
        return klass.ptr();
      }
    }
  }

  Library& klass_lib = Library::Handle(zone);
  String& klass_lib_url = String::Handle(zone);
  const String& lib_url = String::Handle(zone, lib.url());
  for (intptr_t i = 0; i < module->class_object_count; i++) {
    entry = module->classes[i];
    if (!entry.IsClass()) continue;
    klass ^= entry.ptr();
    klass_lib = klass.library();
    if (klass_lib.IsNull()) continue;
    klass_lib_url = klass_lib.url();
    if (!klass_lib_url.Equals(lib_url)) continue;
    klass_name = klass.Name();
    if (klass_name.Equals(name)) {
      return klass.ptr();
    }
  }

  ClassTable* class_table = Thread::Current()->isolate_group()->class_table();
  const intptr_t cid_end = class_table->NumCids();
  for (intptr_t cid = kNumPredefinedCids; cid < cid_end; cid++) {
    entry = class_table->At(cid);
    if (!entry.IsClass()) continue;
    klass ^= entry.ptr();
    klass_lib = klass.library();
    if (klass_lib.IsNull()) continue;
    klass_lib_url = klass_lib.url();
    if (!klass_lib_url.Equals(lib_url)) continue;
    klass_name = klass.Name();
    if (klass_name.Equals(name)) {
      return klass.ptr();
    }
  }
  return Class::null();
}

static ObjectPtr ReadModuleExportedValue(LoadedModule* module,
                                         Zone* zone,
                                         const Object& target,
                                         ModuleExportKind kind) {
  switch (kind) {
    case ModuleExportKind::kField: {
      if (!target.IsField()) return Object::sentinel().ptr();
      const Field& field = Field::Cast(target);
      return ModuleStaticFieldValue(module, field);
    }
    case ModuleExportKind::kGetter: {
      if (!target.IsFunction()) return Object::sentinel().ptr();
      const Function& getter = Function::Cast(target);
      const Object& result = Object::Handle(
          zone, DartEntry::InvokeFunction(getter, Object::empty_array()));
      if (result.IsError()) {
        Exceptions::PropagateError(Error::Cast(result));
        UNREACHABLE();
      }
      return result.ptr();
    }
    case ModuleExportKind::kFunction:
      break;
  }
  return Object::sentinel().ptr();
}

static ObjectPtr LookupModuleExportedValue(LoadedModule* module,
                                           Zone* zone,
                                           const String& name) {
  Object& target = Object::Handle(
      zone, LookupModuleExportTarget(module, zone, name,
                                     ModuleExportKind::kField));
  if (!target.IsNull()) {
    const Object& value = Object::Handle(
        zone, ReadModuleExportedValue(module, zone, target,
                                      ModuleExportKind::kField));
    if (!value.IsSentinel()) return value.ptr();
  }

  target = LookupModuleExportTarget(module, zone, name,
                                    ModuleExportKind::kGetter);
  if (!target.IsNull()) {
    return ReadModuleExportedValue(module, zone, target,
                                   ModuleExportKind::kGetter);
  }
  return Object::sentinel().ptr();
}

// Converts GrowableObjectArray to a fixed Array (used for args / arg_names).
static ArrayPtr GrowableToArray(Zone* zone, const GrowableObjectArray& list) {
  const intptr_t len = list.Length();
  const Array& arr = Array::Handle(zone, Array::New(len));
  Object& obj = Object::Handle(zone);
  for (intptr_t i = 0; i < len; i++) {
    obj = list.At(i);
    arr.SetAt(i, obj);
  }
  return arr.ptr();
}

// Builds a combined args Array: positional values followed by named values.
static ArrayPtr BuildArgArray(Zone* zone,
                              const GrowableObjectArray& positional,
                              const GrowableObjectArray& named_values) {
  const intptr_t n_pos = positional.Length();
  const intptr_t n_named = named_values.Length();
  const Array& arr = Array::Handle(zone, Array::New(n_pos + n_named));
  Object& obj = Object::Handle(zone);
  for (intptr_t i = 0; i < n_pos; i++) {
    obj = positional.At(i);
    arr.SetAt(i, obj);
  }
  for (intptr_t i = 0; i < n_named; i++) {
    obj = named_values.At(i);
    arr.SetAt(n_pos + i, obj);
  }
  return arr.ptr();
}

static ObjectPtr InvokeModuleFunction(Zone* zone,
                                      const Function& function,
                                      const Array& args,
                                      const Array& names) {
  const Array& desc = Array::Handle(
      zone, ArgumentsDescriptor::NewBoxed(/*type_args_len=*/0, args.Length(),
                                          names));
  const Object& result =
      Object::Handle(zone, DartEntry::InvokeFunction(function, args, desc));
  if (result.IsError()) {
    Exceptions::PropagateError(Error::Cast(result));
    UNREACHABLE();
  }
  return result.ptr();
}

[[noreturn]] static void ThrowSymbolNotFound(const char* kind,
                                             const char* name) {
  const String& msg = String::Handle(
      String::NewFormatted("No %s '%s' found in module", kind, name));
  Exceptions::ThrowArgumentError(msg);
  UNREACHABLE();
}

// ---------------------------------------------------------------------------
// Module_getValue – look up a top-level field/getter in the module.
// Native args: receiver(0), valueName(1)
// ---------------------------------------------------------------------------
DEFINE_NATIVE_ENTRY(Module_getValue, 0, 2) {
  if (!FLAG_precompiled_mode) ThrowModuleAotOnly();
  GET_NON_NULL_NATIVE_ARGUMENT(Instance, module_obj, arguments->NativeArgAt(0));
  GET_NON_NULL_NATIVE_ARGUMENT(String, value_name, arguments->NativeArgAt(1));

  LoadedModule* m = GetLoadedModuleFromObject(thread, zone, module_obj);
  const Object& exported_value =
      Object::Handle(zone, LookupModuleExportedValue(m, zone, value_name));
  if (!exported_value.IsSentinel()) {
    return exported_value.ptr();
  }

  const GrowableObjectArray& libs =
      GrowableObjectArray::Handle(zone, m->object_store->libraries());
  if (!libs.IsNull()) {
    Library& lib = Library::Handle(zone);
    for (intptr_t i = 0; i < libs.Length(); i++) {
      lib ^= libs.At(i);
      const Field& field =
          Field::Handle(zone, LookupModuleStaticField(zone, lib, value_name));
      if (!field.IsNull()) {
        const Object& value =
            Object::Handle(zone, ModuleStaticFieldValue(m, field));
        if (!value.IsSentinel()) {
          return value.ptr();
        }
      }

      const String& getter_name =
          String::Handle(zone, Field::GetterName(value_name));
      const Function& getter = Function::Handle(
          zone, LookupModuleStaticFunction(zone, lib, getter_name));
      if (!getter.IsNull()) {
        const Object& result = Object::Handle(
            zone, DartEntry::InvokeFunction(getter, Object::empty_array()));
        if (result.IsError()) {
          Exceptions::PropagateError(Error::Cast(result));
          UNREACHABLE();
        }
        return result.ptr();
      }

      const Object& result =
          Object::Handle(zone, lib.InvokeGetter(value_name,
                                                /*check_is_entrypoint=*/false,
                                                /*respect_reflectable=*/false,
                                                /*for_invocation=*/true));
      if (result.ptr() == Object::sentinel().ptr()) continue;
      if (result.IsError()) {
        Exceptions::PropagateError(Error::Cast(result));
        UNREACHABLE();
      }
      return result.ptr();
    }
  }
  ThrowSymbolNotFound("value", value_name.ToCString());
}

// ---------------------------------------------------------------------------
// Module_lookupFunction – look up a top-level function and return its closure.
// Native args: receiver(0), exportName(1)
// ---------------------------------------------------------------------------
DEFINE_NATIVE_ENTRY(Module_lookupFunction, 0, 2) {
  if (!FLAG_precompiled_mode) ThrowModuleAotOnly();
  GET_NON_NULL_NATIVE_ARGUMENT(Instance, module_obj, arguments->NativeArgAt(0));
  GET_NON_NULL_NATIVE_ARGUMENT(String, export_name, arguments->NativeArgAt(1));

  LoadedModule* m = GetLoadedModuleFromObject(thread, zone, module_obj);
  Function& function = Function::Handle(
      zone, LookupModuleTopLevelFunction(m, zone, export_name));
  if (function.IsNull()) {
    ThrowSymbolNotFound("function", export_name.ToCString());
  }
  if (!function.is_static() ||
      function.kind() != UntaggedFunction::kRegularFunction) {
    const String& msg = String::Handle(
        zone, String::NewFormatted("Module export '%s' is not a top-level "
                                   "regular function",
                                   export_name.ToCString()));
    Exceptions::ThrowArgumentError(msg);
    UNREACHABLE();
  }

  const Class& owner = Class::Handle(zone, function.Owner());
  const Error& error = Error::Handle(zone, owner.EnsureIsFinalized(thread));
  if (!error.IsNull()) {
    Exceptions::PropagateError(error);
    UNREACHABLE();
  }

  if (!function.SafeToClosurize()) {
    const String& msg = String::Handle(
        zone, String::NewFormatted("Module function '%s' was not retained for "
                                   "tear-off lookup. Annotate it with "
                                   "@pragma('vm:entry-point') or "
                                   "@pragma('vm:entry-point', 'get').",
                                   export_name.ToCString()));
    Exceptions::ThrowArgumentError(msg);
    UNREACHABLE();
  }

  const Function& closure_function =
      Function::Handle(zone, function.ImplicitClosureFunction());
  if (closure_function.IsNull()) {
    ThrowSymbolNotFound("function closure", export_name.ToCString());
  }
  return closure_function.ImplicitStaticClosure();
}

// ---------------------------------------------------------------------------
// Module_invokeMethod – invoke a top-level function.
// Native args: receiver(0), name(1), args(2), argNames(3), argValues(4)
// ---------------------------------------------------------------------------
DEFINE_NATIVE_ENTRY(Module_invokeMethod, 0, 5) {
  if (!FLAG_precompiled_mode) ThrowModuleAotOnly();
  GET_NON_NULL_NATIVE_ARGUMENT(Instance, module_obj, arguments->NativeArgAt(0));
  GET_NON_NULL_NATIVE_ARGUMENT(String, func_name, arguments->NativeArgAt(1));
  GET_NON_NULL_NATIVE_ARGUMENT(GrowableObjectArray, pos_args,
                               arguments->NativeArgAt(2));
  GET_NON_NULL_NATIVE_ARGUMENT(GrowableObjectArray, arg_names_list,
                               arguments->NativeArgAt(3));
  GET_NON_NULL_NATIVE_ARGUMENT(GrowableObjectArray, named_values,
                               arguments->NativeArgAt(4));

  LoadedModule* m = GetLoadedModuleFromObject(thread, zone, module_obj);
  const Array& args =
      Array::Handle(zone, BuildArgArray(zone, pos_args, named_values));
  const Array& names =
      Array::Handle(zone, GrowableToArray(zone, arg_names_list));
  const Function& export_function =
      Function::Handle(zone, LookupModuleExportFunction(m, zone, func_name));
  if (!export_function.IsNull()) {
    return InvokeModuleFunction(zone, export_function, args, names);
  }

  const GrowableObjectArray& libs =
      GrowableObjectArray::Handle(zone, m->object_store->libraries());
  if (!libs.IsNull()) {
    Library& lib = Library::Handle(zone);
    for (intptr_t i = 0; i < libs.Length(); i++) {
      lib ^= libs.At(i);
      const Function& function = Function::Handle(
          zone, LookupModuleStaticFunction(zone, lib, func_name));
      if (function.IsNull() &&
          LookupModuleStaticField(zone, lib, func_name) == Field::null()) {
        continue;
      }
      if (!function.IsNull()) {
        return InvokeModuleFunction(zone, function, args, names);
      }
      const Object& result =
          Object::Handle(zone, lib.Invoke(func_name, args, names,
                                          /*check_is_entrypoint=*/false,
                                          /*respect_reflectable=*/false));
      if (result.IsError()) {
        Exceptions::PropagateError(Error::Cast(result));
        UNREACHABLE();
      }
      return result.ptr();
    }
  }
  ThrowSymbolNotFound("function", func_name.ToCString());
}

// ---------------------------------------------------------------------------
// Module_invokeStaticMethod – invoke a static method on a module class.
// Native args: receiver(0), className(1), name(2), args(3), argNames(4),
//              argValues(5)
// ---------------------------------------------------------------------------
DEFINE_NATIVE_ENTRY(Module_invokeStaticMethod, 0, 6) {
  if (!FLAG_precompiled_mode) ThrowModuleAotOnly();
  GET_NON_NULL_NATIVE_ARGUMENT(Instance, module_obj, arguments->NativeArgAt(0));
  GET_NON_NULL_NATIVE_ARGUMENT(String, class_name, arguments->NativeArgAt(1));
  GET_NON_NULL_NATIVE_ARGUMENT(String, method_name, arguments->NativeArgAt(2));
  GET_NON_NULL_NATIVE_ARGUMENT(GrowableObjectArray, pos_args,
                               arguments->NativeArgAt(3));
  GET_NON_NULL_NATIVE_ARGUMENT(GrowableObjectArray, arg_names_list,
                               arguments->NativeArgAt(4));
  GET_NON_NULL_NATIVE_ARGUMENT(GrowableObjectArray, named_values,
                               arguments->NativeArgAt(5));

  LoadedModule* m = GetLoadedModuleFromObject(thread, zone, module_obj);
  const GrowableObjectArray& libs =
      GrowableObjectArray::Handle(zone, m->object_store->libraries());
  if (!libs.IsNull()) {
    Library& lib = Library::Handle(zone);
    Class& klass = Class::Handle(zone);
    for (intptr_t i = 0; i < libs.Length(); i++) {
      lib ^= libs.At(i);
      klass = LookupModuleClass(m, zone, lib, class_name);
      if (klass.IsNull()) continue;
      const Array& args =
          Array::Handle(zone, BuildArgArray(zone, pos_args, named_values));
      const Array& names =
          Array::Handle(zone, GrowableToArray(zone, arg_names_list));
      const Object& result =
          Object::Handle(zone, klass.Invoke(method_name, args, names,
                                            /*check_is_entrypoint=*/false,
                                            /*respect_reflectable=*/false));
      if (result.IsError()) {
        Exceptions::PropagateError(Error::Cast(result));
        UNREACHABLE();
      }
      return result.ptr();
    }
  }
  ThrowSymbolNotFound("class", class_name.ToCString());
}

// ---------------------------------------------------------------------------
// Module_invokeConstructor – invoke a constructor on a module class.
// Native args: receiver(0), className(1), constructorName(2), args(3),
//              argNames(4), argValues(5)
// Constructor name convention (mirrors): "ClassName." for unnamed,
// "ClassName.ctorName" for named.
// ---------------------------------------------------------------------------
DEFINE_NATIVE_ENTRY(Module_invokeConstructor, 0, 6) {
  if (!FLAG_precompiled_mode) ThrowModuleAotOnly();
  GET_NON_NULL_NATIVE_ARGUMENT(Instance, module_obj, arguments->NativeArgAt(0));
  GET_NATIVE_ARGUMENT(String, class_name, arguments->NativeArgAt(1));
  GET_NATIVE_ARGUMENT(String, ctor_name_suffix, arguments->NativeArgAt(2));
  GET_NON_NULL_NATIVE_ARGUMENT(GrowableObjectArray, pos_args,
                               arguments->NativeArgAt(3));
  GET_NON_NULL_NATIVE_ARGUMENT(GrowableObjectArray, arg_names_list,
                               arguments->NativeArgAt(4));
  GET_NON_NULL_NATIVE_ARGUMENT(GrowableObjectArray, named_values,
                               arguments->NativeArgAt(5));

  if (class_name.IsNull()) {
    const String& msg =
        String::Handle(String::New("invokeConstructor requires a className"));
    Exceptions::ThrowArgumentError(msg);
    UNREACHABLE();
  }

  LoadedModule* m = GetLoadedModuleFromObject(thread, zone, module_obj);
  const GrowableObjectArray& libs =
      GrowableObjectArray::Handle(zone, m->object_store->libraries());
  if (!libs.IsNull()) {
    Library& lib = Library::Handle(zone);
    Class& klass = Class::Handle(zone);
    for (intptr_t i = 0; i < libs.Length(); i++) {
      lib ^= libs.At(i);
      klass = LookupModuleClass(m, zone, lib, class_name);
      if (klass.IsNull()) continue;

      // Build internal constructor name: "ClassName." or "ClassName.ctorName".
      String& internal_ctor_name =
          String::Handle(zone, String::Concat(class_name, Symbols::Dot()));
      if (!ctor_name_suffix.IsNull() && ctor_name_suffix.Length() > 0) {
        internal_ctor_name =
            String::Concat(internal_ctor_name, ctor_name_suffix);
      }

      Function& ctor = Function::Handle(
          zone, Resolver::ResolveFunction(zone, klass, internal_ctor_name));
      if (ctor.IsNull() || ctor.kind() != UntaggedFunction::kConstructor) {
        ThrowSymbolNotFound("constructor", internal_ctor_name.ToCString());
      }

      const Error& finalize_err =
          Error::Handle(zone, klass.EnsureIsAllocateFinalized(thread));
      if (!finalize_err.IsNull()) {
        Exceptions::PropagateError(finalize_err);
        UNREACHABLE();
      }

      const intptr_t n_explicit = pos_args.Length() + named_values.Length();
      // For generative constructors: args[0] = new instance.
      // For factories: args[0] = type arguments.
      const intptr_t n_total = 1 + n_explicit;
      const Array& args = Array::Handle(zone, Array::New(n_total));
      const Array& names =
          Array::Handle(zone, GrowableToArray(zone, arg_names_list));

      if (ctor.IsGenerativeConstructor()) {
        const Instance& new_obj = Instance::Handle(zone, Instance::New(klass));
        args.SetAt(0, new_obj);
        Object& val = Object::Handle(zone);
        for (intptr_t j = 0; j < pos_args.Length(); j++) {
          val = pos_args.At(j);
          args.SetAt(1 + j, val);
        }
        for (intptr_t j = 0; j < named_values.Length(); j++) {
          val = named_values.At(j);
          args.SetAt(1 + pos_args.Length() + j, val);
        }

        const Array& desc = Array::Handle(
            zone,
            ArgumentsDescriptor::NewBoxed(/*type_args_len=*/0, n_total, names));
        const Object& result =
            Object::Handle(zone, DartEntry::InvokeFunction(ctor, args, desc));
        if (result.IsError()) {
          Exceptions::PropagateError(Error::Cast(result));
          UNREACHABLE();
        }
        return new_obj.ptr();
      } else {
        // Factory: args[0] = TypeArguments::null() (no explicit type args).
        args.SetAt(0, Object::null_object());
        Object& val = Object::Handle(zone);
        for (intptr_t j = 0; j < pos_args.Length(); j++) {
          val = pos_args.At(j);
          args.SetAt(1 + j, val);
        }
        for (intptr_t j = 0; j < named_values.Length(); j++) {
          val = named_values.At(j);
          args.SetAt(1 + pos_args.Length() + j, val);
        }

        const Array& desc = Array::Handle(
            zone,
            ArgumentsDescriptor::NewBoxed(/*type_args_len=*/0, n_total, names));
        const Object& result =
            Object::Handle(zone, DartEntry::InvokeFunction(ctor, args, desc));
        if (result.IsError()) {
          Exceptions::PropagateError(Error::Cast(result));
          UNREACHABLE();
        }
        return result.ptr();
      }
    }
  }
  ThrowSymbolNotFound("class", class_name.ToCString());
}

#endif

}  // namespace dart

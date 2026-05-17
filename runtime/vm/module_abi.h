// Copyright (c) 2026, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef RUNTIME_VM_MODULE_ABI_H_
#define RUNTIME_VM_MODULE_ABI_H_

#include "platform/assert.h"
#include "vm/globals.h"

namespace dart {

class IsolateGroup;

// Kind tags used by ABI manifest tables and future snapshot import records.
enum class ModuleAbiObjectKind : uint8_t {
  kLibrary = 0,
  kClass = 1,
  kField = 2,
  kFunction = 3,
  kSelector = 4,
  kType = 5,
  kTypeArguments = 6,
};

enum class ModuleAbiPayloadSectionKind : uint16_t {
  kAbiImports = 1,
};

// Runtime export table tags used by LoadedModule::exports. The table is a flat
// VM Array with ModuleExportTable::kEntryLength elements per entry.
enum class ModuleExportKind : intptr_t {
  kFunction = 0,
  kField = 1,
  kGetter = 2,
};

class ModuleExportTable {
 public:
  static constexpr intptr_t kKindIndex = 0;
  static constexpr intptr_t kNameIndex = 1;
  static constexpr intptr_t kTargetIndex = 2;
  static constexpr intptr_t kEntryLength = 3;

 private:
  DISALLOW_ALLOCATION();
  DISALLOW_IMPLICIT_CONSTRUCTORS(ModuleExportTable);
};

// Runtime import table layout used by LoadedModule::abi_imports. The table is a
// flat VM Array with ModuleAbiImportTable::kEntryLength elements per entry.
class ModuleAbiImportTable {
 public:
  static constexpr intptr_t kKindIndex = 0;
  static constexpr intptr_t kManifestIdIndex = 1;
  static constexpr intptr_t kExpectedHashIndex = 2;
  static constexpr intptr_t kResolvedObjectIndex = 3;
  static constexpr intptr_t kEntryLength = 4;

 private:
  DISALLOW_ALLOCATION();
  DISALLOW_IMPLICIT_CONSTRUCTORS(ModuleAbiImportTable);
};

// Runtime ABI manifest object-table layout. Slot 0 keeps the original manifest
// text for diagnostics/tooling. The remaining slots are host-owned object
// arrays indexed by manifest id for ModuleAbiImportRecord resolution.
class ModuleAbiManifestTable {
 public:
  static constexpr intptr_t kRawJsonIndex = 0;
  static constexpr intptr_t kLibraryIndex =
      1 + static_cast<intptr_t>(ModuleAbiObjectKind::kLibrary);
  static constexpr intptr_t kClassIndex =
      1 + static_cast<intptr_t>(ModuleAbiObjectKind::kClass);
  static constexpr intptr_t kFieldIndex =
      1 + static_cast<intptr_t>(ModuleAbiObjectKind::kField);
  static constexpr intptr_t kFunctionIndex =
      1 + static_cast<intptr_t>(ModuleAbiObjectKind::kFunction);
  static constexpr intptr_t kSelectorIndex =
      1 + static_cast<intptr_t>(ModuleAbiObjectKind::kSelector);
  static constexpr intptr_t kTypeIndex =
      1 + static_cast<intptr_t>(ModuleAbiObjectKind::kType);
  static constexpr intptr_t kTypeArgumentsIndex =
      1 + static_cast<intptr_t>(ModuleAbiObjectKind::kTypeArguments);
  static constexpr intptr_t kLength =
      kTypeArgumentsIndex + static_cast<intptr_t>(1);

  static constexpr intptr_t ObjectKindToTableIndex(uint16_t object_kind) {
    return object_kind <=
                   static_cast<uint16_t>(ModuleAbiObjectKind::kTypeArguments)
               ? static_cast<intptr_t>(object_kind) + 1
               : -1;
  }

 private:
  DISALLOW_ALLOCATION();
  DISALLOW_IMPLICIT_CONSTRUCTORS(ModuleAbiManifestTable);
};

// Fixed-size prefix for the kDartModuleAbiData symbol.
//
// Multi-byte fields are little-endian. The payload format is intentionally not
// interpreted by the runtime yet; this header gives the loader a stable place
// to validate format versioning and preserve the manifest hash for later
// selector/CID relocation work.
struct ModuleAbiHeader {
  uint16_t format_version = 0;
  uint16_t flags = 0;
  uint32_t header_size = 0;
  uint32_t payload_size = 0;
  uint64_t manifest_hash = 0;
  uint64_t sdk_hash = 0;
  uint64_t compiler_flags_hash = 0;
  uint16_t target_arch = 0;
  uint16_t target_os = 0;
  uint32_t reserved = 0;

  intptr_t TotalSize() const {
    return static_cast<intptr_t>(header_size) +
           static_cast<intptr_t>(payload_size);
  }
};

// Optional fixed-size payload immediately following ModuleAbiHeader. These
// counts let the loader reserve isolate-group runtime id ranges before the
// module object graph is deserialized.
//
// Payload layout:
//   0..3   module-private non-top-level class count
//   4..7   module-private selector count
//   8..11  serialized module dispatch table entry count
//   12..15 reserved
struct ModuleAbiRuntimeIds {
  uint32_t private_class_count = 0;
  uint32_t private_selector_count = 0;
  uint32_t dispatch_table_entry_count = 0;
  uint32_t reserved = 0;
};

// Header for optional payload sections following ModuleAbiRuntimeIds.
//
// Payload-section layout:
//   0..1   ModuleAbiPayloadSectionKind
//   2..3   reserved flags, currently zero
//   4..7   fixed entry size in bytes
//   8..11  entry count
//   12..15 payload size in bytes
//   16..   payload bytes
struct ModuleAbiPayloadSection {
  uint16_t kind = 0;
  uint16_t flags = 0;
  uint32_t entry_size = 0;
  uint32_t entry_count = 0;
  uint32_t payload_size = 0;
  const uint8_t* payload = nullptr;

  bool IsNull() const { return kind == 0; }
};

// Fixed-size ABI import record. The manifest id is interpreted inside the
// table named by |object_kind| in the host ABI manifest.
//
// Import-record layout:
//   0..1   ModuleAbiObjectKind
//   2..3   reserved flags, currently zero
//   4..7   manifest table id
//   8..15  expected per-object ABI hash, or zero when not available yet
struct ModuleAbiImportRecord {
  uint16_t object_kind = 0;
  uint16_t flags = 0;
  uint32_t manifest_id = 0;
  uint64_t expected_hash = 0;
};

class ModuleAbi {
 public:
  static constexpr intptr_t kHeaderSize = 48;
  static constexpr intptr_t kRuntimeIdsSize = 16;
  static constexpr intptr_t kPayloadSectionHeaderSize = 16;
  static constexpr intptr_t kImportRecordSize = 16;
  static constexpr intptr_t kRuntimeIdsAndEmptyImportSectionSize =
      kRuntimeIdsSize + kPayloadSectionHeaderSize;
  static constexpr uint16_t kCurrentFormatVersion = 3;

  enum HeaderFlag : uint16_t {
    kCompressedPointers = 1 << 0,
    kProductMode = 1 << 1,
    kSoundNullSafety = 1 << 2,
  };

  enum TargetArch : uint16_t {
    kTargetArchUnknown = 0,
    kTargetArchIA32 = 1,
    kTargetArchX64 = 2,
    kTargetArchARM = 3,
    kTargetArchARM64 = 4,
    kTargetArchRISCV32 = 5,
    kTargetArchRISCV64 = 6,
  };

  enum TargetOs : uint16_t {
    kTargetOsUnknown = 0,
    kTargetOsAndroid = 1,
    kTargetOsFuchsia = 2,
    kTargetOsIOS = 3,
    kTargetOsLinux = 4,
    kTargetOsMacOS = 5,
    kTargetOsWindows = 6,
  };

  // Header layout:
  //   0..3   "DMAB"
  //   4..5   format version
  //   6..7   flags
  //   8..11  header size
  //   12..15 payload size
  //   16..23 ABI manifest hash
  //   24..31 SDK hash
  //   32..39 compiler flags hash
  //   40..41 target architecture
  //   42..43 target OS
  //   44..47 reserved
  static const char* ReadHeader(const uint8_t* data, ModuleAbiHeader* out);
  static const char* ReadRuntimeIds(const uint8_t* data,
                                    const ModuleAbiHeader& header,
                                    ModuleAbiRuntimeIds* out);
  static const char* FindPayloadSection(const uint8_t* data,
                                        const ModuleAbiHeader& header,
                                        ModuleAbiPayloadSectionKind kind,
                                        ModuleAbiPayloadSection* out);
  static const char* ReadImportRecord(const ModuleAbiPayloadSection& section,
                                      intptr_t index,
                                      ModuleAbiImportRecord* out);
  static const char* ValidatePayload(const uint8_t* data,
                                     const ModuleAbiHeader& header);
  static void WriteHeader(uint8_t* data,
                          uint64_t manifest_hash,
                          uint32_t payload_size = 0);
  static void WriteHeaderAndRuntimeIds(uint8_t* data,
                                       uint64_t manifest_hash,
                                       const ModuleAbiRuntimeIds& runtime_ids);
  static void WriteHeaderRuntimeIdsAndEmptyImportSection(
      uint8_t* data,
      uint64_t manifest_hash,
      const ModuleAbiRuntimeIds& runtime_ids);
  static const char* ValidateCompatibility(const ModuleAbiHeader& header,
                                           uint64_t host_manifest_hash,
                                           IsolateGroup* isolate_group);

 private:
  DISALLOW_ALLOCATION();
  DISALLOW_IMPLICIT_CONSTRUCTORS(ModuleAbi);
};

}  // namespace dart

#endif  // RUNTIME_VM_MODULE_ABI_H_

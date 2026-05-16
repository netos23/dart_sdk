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

class ModuleAbi {
 public:
  static constexpr intptr_t kHeaderSize = 48;
  static constexpr uint16_t kCurrentFormatVersion = 2;

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
  static void WriteHeader(uint8_t* data,
                          uint64_t manifest_hash,
                          uint32_t payload_size = 0);
  static const char* ValidateCompatibility(const ModuleAbiHeader& header,
                                           uint64_t host_manifest_hash,
                                           IsolateGroup* isolate_group);

 private:
  DISALLOW_ALLOCATION();
  DISALLOW_IMPLICIT_CONSTRUCTORS(ModuleAbi);
};

}  // namespace dart

#endif  // RUNTIME_VM_MODULE_ABI_H_

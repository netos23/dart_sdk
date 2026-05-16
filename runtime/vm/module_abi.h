// Copyright (c) 2026, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef RUNTIME_VM_MODULE_ABI_H_
#define RUNTIME_VM_MODULE_ABI_H_

#include "platform/assert.h"
#include "vm/globals.h"

namespace dart {

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

// Fixed-size prefix for the optional kDartModuleAbiData symbol.
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

  intptr_t TotalSize() const {
    return static_cast<intptr_t>(header_size) +
           static_cast<intptr_t>(payload_size);
  }
};

class ModuleAbi {
 public:
  static constexpr intptr_t kHeaderSize = 24;
  static constexpr uint16_t kCurrentFormatVersion = 1;

  // Header layout:
  //   0..3   "DMAB"
  //   4..5   format version
  //   6..7   flags
  //   8..11  header size
  //   12..15 payload size
  //   16..23 ABI manifest hash
  static const char* ReadHeader(const uint8_t* data, ModuleAbiHeader* out);
  static void WriteHeader(uint8_t* data,
                          uint64_t manifest_hash,
                          uint16_t flags = 0,
                          uint32_t payload_size = 0);

 private:
  DISALLOW_ALLOCATION();
  DISALLOW_IMPLICIT_CONSTRUCTORS(ModuleAbi);
};

}  // namespace dart

#endif  // RUNTIME_VM_MODULE_ABI_H_

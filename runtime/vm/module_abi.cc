// Copyright (c) 2026, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/module_abi.h"

#include <cstdlib>
#include <cstring>

#include "vm/dart.h"
#include "vm/version.h"

namespace dart {
namespace {

uint16_t ReadUint16LE(const uint8_t* data) {
  return static_cast<uint16_t>(data[0]) |
         static_cast<uint16_t>(static_cast<uint16_t>(data[1]) << 8);
}

uint32_t ReadUint32LE(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) |
         (static_cast<uint32_t>(data[3]) << 24);
}

uint64_t ReadUint64LE(const uint8_t* data) {
  uint64_t value = 0;
  for (intptr_t i = 7; i >= 0; i--) {
    value <<= 8;
    value |= data[i];
  }
  return value;
}

void WriteUint16LE(uint8_t* data, uint16_t value) {
  data[0] = static_cast<uint8_t>(value);
  data[1] = static_cast<uint8_t>(value >> 8);
}

void WriteUint32LE(uint8_t* data, uint32_t value) {
  for (intptr_t i = 0; i < 4; i++) {
    data[i] = static_cast<uint8_t>(value >> (8 * i));
  }
}

void WriteUint64LE(uint8_t* data, uint64_t value) {
  for (intptr_t i = 0; i < 8; i++) {
    data[i] = static_cast<uint8_t>(value >> (8 * i));
  }
}

bool HasModuleAbiMagic(const uint8_t* data) {
  return data[0] == 'D' && data[1] == 'M' && data[2] == 'A' && data[3] == 'B';
}

uint64_t HashCString(const char* value) {
  ASSERT(value != nullptr);
  static constexpr uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
  static constexpr uint64_t kFnvPrime = 1099511628211ULL;
  uint64_t hash = kFnvOffsetBasis;
  for (const char* cursor = value; *cursor != '\0'; cursor++) {
    hash ^= static_cast<uint8_t>(*cursor);
    hash *= kFnvPrime;
  }
  return hash;
}

uint64_t CurrentSdkHash() {
  const char* sdk_hash = Version::SdkHash();
  return HashCString(sdk_hash == nullptr ? "" : sdk_hash);
}

uint64_t CurrentCompilerFlagsHash(IsolateGroup* isolate_group) {
  char* features =
      Dart::FeaturesString(isolate_group, /*is_vm_isolate=*/false,
                           Snapshot::kFullAOTModule);
  ASSERT(features != nullptr);
  const uint64_t hash = HashCString(features);
  free(features);
  return hash;
}

uint16_t CurrentTargetArch() {
#if defined(TARGET_ARCH_IA32)
  return ModuleAbi::kTargetArchIA32;
#elif defined(TARGET_ARCH_X64)
  return ModuleAbi::kTargetArchX64;
#elif defined(TARGET_ARCH_ARM)
  return ModuleAbi::kTargetArchARM;
#elif defined(TARGET_ARCH_ARM64)
  return ModuleAbi::kTargetArchARM64;
#elif defined(TARGET_ARCH_RISCV32)
  return ModuleAbi::kTargetArchRISCV32;
#elif defined(TARGET_ARCH_RISCV64)
  return ModuleAbi::kTargetArchRISCV64;
#else
#error What architecture?
#endif
}

uint16_t CurrentTargetOs() {
#if defined(DART_TARGET_OS_ANDROID)
  return ModuleAbi::kTargetOsAndroid;
#elif defined(DART_TARGET_OS_FUCHSIA)
  return ModuleAbi::kTargetOsFuchsia;
#elif defined(DART_TARGET_OS_MACOS)
#if defined(DART_TARGET_OS_MACOS_IOS)
  return ModuleAbi::kTargetOsIOS;
#else
  return ModuleAbi::kTargetOsMacOS;
#endif
#elif defined(DART_TARGET_OS_LINUX)
  return ModuleAbi::kTargetOsLinux;
#elif defined(DART_TARGET_OS_WINDOWS)
  return ModuleAbi::kTargetOsWindows;
#else
#error What operating system?
#endif
}

uint16_t CurrentFlags() {
  uint16_t flags = ModuleAbi::kSoundNullSafety;
#if defined(DART_COMPRESSED_POINTERS)
  flags |= ModuleAbi::kCompressedPointers;
#endif
#if defined(PRODUCT)
  flags |= ModuleAbi::kProductMode;
#endif
  return flags;
}

bool FlagMatches(uint16_t left, uint16_t right, ModuleAbi::HeaderFlag flag) {
  return ((left ^ right) & flag) == 0;
}

void WriteHeaderFields(uint8_t* data, const ModuleAbiHeader& header) {
  ASSERT(data != nullptr);
  data[0] = 'D';
  data[1] = 'M';
  data[2] = 'A';
  data[3] = 'B';
  WriteUint16LE(data + 4, header.format_version);
  WriteUint16LE(data + 6, header.flags);
  WriteUint32LE(data + 8, header.header_size);
  WriteUint32LE(data + 12, header.payload_size);
  WriteUint64LE(data + 16, header.manifest_hash);
  WriteUint64LE(data + 24, header.sdk_hash);
  WriteUint64LE(data + 32, header.compiler_flags_hash);
  WriteUint16LE(data + 40, header.target_arch);
  WriteUint16LE(data + 42, header.target_os);
  WriteUint32LE(data + 44, header.reserved);
}

}  // namespace

const char* ModuleAbi::ReadHeader(const uint8_t* data, ModuleAbiHeader* out) {
  ASSERT(out != nullptr);
  if (data == nullptr) {
    return "missing module ABI data";
  }
  if (!HasModuleAbiMagic(data)) {
    return "invalid module ABI magic";
  }

  ModuleAbiHeader header;
  header.format_version = ReadUint16LE(data + 4);
  header.flags = ReadUint16LE(data + 6);
  header.header_size = ReadUint32LE(data + 8);
  header.payload_size = ReadUint32LE(data + 12);
  header.manifest_hash = ReadUint64LE(data + 16);

  if (header.format_version != kCurrentFormatVersion) {
    return "unsupported module ABI version";
  }
  if (header.header_size < kHeaderSize) {
    return "invalid module ABI header size";
  }
  if ((static_cast<uint64_t>(header.header_size) +
       static_cast<uint64_t>(header.payload_size)) >
      static_cast<uint64_t>(kIntptrMax)) {
    return "module ABI payload is too large";
  }

  header.sdk_hash = ReadUint64LE(data + 24);
  header.compiler_flags_hash = ReadUint64LE(data + 32);
  header.target_arch = ReadUint16LE(data + 40);
  header.target_os = ReadUint16LE(data + 42);
  header.reserved = ReadUint32LE(data + 44);

  *out = header;
  return nullptr;
}

const char* ModuleAbi::ReadRuntimeIds(const uint8_t* data,
                                      const ModuleAbiHeader& header,
                                      ModuleAbiRuntimeIds* out) {
  ASSERT(out != nullptr);
  ModuleAbiRuntimeIds runtime_ids;
  if (header.payload_size == 0) {
    *out = runtime_ids;
    return nullptr;
  }
  if (data == nullptr) {
    return "missing module ABI data";
  }
  if (header.payload_size < kRuntimeIdsSize) {
    return "invalid module ABI runtime-id payload size";
  }

  const uint8_t* payload = data + header.header_size;
  runtime_ids.private_class_count = ReadUint32LE(payload);
  runtime_ids.private_selector_count = ReadUint32LE(payload + 4);
  runtime_ids.dispatch_table_entry_count = ReadUint32LE(payload + 8);
  runtime_ids.reserved = ReadUint32LE(payload + 12);
  if (runtime_ids.reserved != 0) {
    return "invalid module ABI runtime-id reserved field";
  }

  *out = runtime_ids;
  return nullptr;
}

void ModuleAbi::WriteHeader(uint8_t* data,
                            uint64_t manifest_hash,
                            uint32_t payload_size) {
  ModuleAbiHeader header;
  header.format_version = kCurrentFormatVersion;
  header.flags = CurrentFlags();
  header.header_size = kHeaderSize;
  header.payload_size = payload_size;
  header.manifest_hash = manifest_hash;
  header.sdk_hash = CurrentSdkHash();
  header.compiler_flags_hash =
      CurrentCompilerFlagsHash(IsolateGroup::Current());
  header.target_arch = CurrentTargetArch();
  header.target_os = CurrentTargetOs();
  header.reserved = 0;
  WriteHeaderFields(data, header);
}

void ModuleAbi::WriteHeaderAndRuntimeIds(
    uint8_t* data,
    uint64_t manifest_hash,
    const ModuleAbiRuntimeIds& runtime_ids) {
  WriteHeader(data, manifest_hash, kRuntimeIdsSize);
  uint8_t* payload = data + kHeaderSize;
  WriteUint32LE(payload, runtime_ids.private_class_count);
  WriteUint32LE(payload + 4, runtime_ids.private_selector_count);
  WriteUint32LE(payload + 8, runtime_ids.dispatch_table_entry_count);
  WriteUint32LE(payload + 12, runtime_ids.reserved);
}

const char* ModuleAbi::ValidateCompatibility(const ModuleAbiHeader& header,
                                             uint64_t host_manifest_hash,
                                             IsolateGroup* isolate_group) {
  if (header.sdk_hash != CurrentSdkHash()) {
    return "module SDK hash does not match current SDK";
  }
  if (header.target_arch != CurrentTargetArch()) {
    return "module target architecture does not match current VM";
  }
  if (header.target_os != CurrentTargetOs()) {
    return "module target OS does not match current VM";
  }

  const uint16_t current_flags = CurrentFlags();
  if (!FlagMatches(header.flags, current_flags, kCompressedPointers)) {
    return "module compressed-pointers mode does not match current VM";
  }
  if (!FlagMatches(header.flags, current_flags, kProductMode)) {
    return "module product mode does not match current VM";
  }
  if (!FlagMatches(header.flags, current_flags, kSoundNullSafety)) {
    return "module null-safety mode does not match current VM";
  }

  if (header.compiler_flags_hash != CurrentCompilerFlagsHash(isolate_group)) {
    return "module compiler flags do not match current VM";
  }

  if (header.manifest_hash != 0 && host_manifest_hash == 0) {
    return "module requires an ABI manifest hash but host has none";
  }
  if (host_manifest_hash != 0 && header.manifest_hash == 0) {
    return "host requires an ABI manifest hash but module has none";
  }
  if (host_manifest_hash != 0 && header.manifest_hash != host_manifest_hash) {
    return "module ABI manifest hash does not match host";
  }

  return nullptr;
}

}  // namespace dart

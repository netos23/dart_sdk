// Copyright (c) 2026, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/module_abi.h"

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

  *out = header;
  return nullptr;
}

void ModuleAbi::WriteHeader(uint8_t* data,
                            uint64_t manifest_hash,
                            uint16_t flags,
                            uint32_t payload_size) {
  ASSERT(data != nullptr);
  data[0] = 'D';
  data[1] = 'M';
  data[2] = 'A';
  data[3] = 'B';
  WriteUint16LE(data + 4, kCurrentFormatVersion);
  WriteUint16LE(data + 6, flags);
  WriteUint32LE(data + 8, kHeaderSize);
  WriteUint32LE(data + 12, payload_size);
  WriteUint64LE(data + 16, manifest_hash);
}

}  // namespace dart

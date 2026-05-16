// Copyright (c) 2026, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/module_abi.h"

#include "platform/assert.h"
#include "vm/unit_test.h"

namespace dart {
namespace {

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

void FillModuleAbiHeader(uint8_t* data,
                         uint16_t version,
                         uint16_t flags,
                         uint32_t header_size,
                         uint32_t payload_size,
                         uint64_t manifest_hash) {
  data[0] = 'D';
  data[1] = 'M';
  data[2] = 'A';
  data[3] = 'B';
  WriteUint16LE(data + 4, version);
  WriteUint16LE(data + 6, flags);
  WriteUint32LE(data + 8, header_size);
  WriteUint32LE(data + 12, payload_size);
  WriteUint64LE(data + 16, manifest_hash);
}

}  // namespace

UNIT_TEST_CASE(ModuleAbiReadHeader) {
  uint8_t data[ModuleAbi::kHeaderSize];
  FillModuleAbiHeader(data, ModuleAbi::kCurrentFormatVersion, 2,
                      ModuleAbi::kHeaderSize, 3, 0x1122334455667788);

  ModuleAbiHeader header;
  EXPECT_NULLPTR(ModuleAbi::ReadHeader(data, &header));
  EXPECT_EQ(ModuleAbi::kCurrentFormatVersion, header.format_version);
  EXPECT_EQ(2, header.flags);
  EXPECT_EQ(ModuleAbi::kHeaderSize, header.header_size);
  EXPECT_EQ(3, header.payload_size);
  EXPECT_EQ(0x1122334455667788, header.manifest_hash);
  EXPECT_EQ(ModuleAbi::kHeaderSize + 3, header.TotalSize());
}

UNIT_TEST_CASE(ModuleAbiRejectsInvalidMagic) {
  uint8_t data[ModuleAbi::kHeaderSize];
  FillModuleAbiHeader(data, ModuleAbi::kCurrentFormatVersion, 0,
                      ModuleAbi::kHeaderSize, 0, 0);
  data[0] = 'X';

  ModuleAbiHeader header;
  EXPECT_STREQ("invalid module ABI magic",
               ModuleAbi::ReadHeader(data, &header));
}

UNIT_TEST_CASE(ModuleAbiRejectsUnsupportedVersion) {
  uint8_t data[ModuleAbi::kHeaderSize];
  FillModuleAbiHeader(data, ModuleAbi::kCurrentFormatVersion + 1, 0,
                      ModuleAbi::kHeaderSize, 0, 0);

  ModuleAbiHeader header;
  EXPECT_STREQ("unsupported module ABI version",
               ModuleAbi::ReadHeader(data, &header));
}

UNIT_TEST_CASE(ModuleAbiRejectsSmallHeader) {
  uint8_t data[ModuleAbi::kHeaderSize];
  FillModuleAbiHeader(data, ModuleAbi::kCurrentFormatVersion, 0,
                      ModuleAbi::kHeaderSize - 1, 0, 0);

  ModuleAbiHeader header;
  EXPECT_STREQ("invalid module ABI header size",
               ModuleAbi::ReadHeader(data, &header));
}

}  // namespace dart

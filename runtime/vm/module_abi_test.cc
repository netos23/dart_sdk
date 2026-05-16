// Copyright (c) 2026, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/module_abi.h"

#include "platform/assert.h"
#include "vm/unit_test.h"

namespace dart {
namespace {
static constexpr uint64_t kTestManifestHash = 0x1122334455667788;

}  // namespace

UNIT_TEST_CASE(ModuleAbiReadHeader) {
  uint8_t data[ModuleAbi::kHeaderSize];
  ModuleAbi::WriteHeader(data, kTestManifestHash, /*flags=*/2,
                         /*payload_size=*/3);

  ModuleAbiHeader header;
  EXPECT_NULLPTR(ModuleAbi::ReadHeader(data, &header));
  EXPECT_EQ(ModuleAbi::kCurrentFormatVersion, header.format_version);
  EXPECT_EQ(static_cast<uint16_t>(2), header.flags);
  EXPECT_EQ(static_cast<uint32_t>(ModuleAbi::kHeaderSize), header.header_size);
  EXPECT_EQ(static_cast<uint32_t>(3), header.payload_size);
  EXPECT_EQ(kTestManifestHash, header.manifest_hash);
  EXPECT_EQ(ModuleAbi::kHeaderSize + 3, header.TotalSize());
}

UNIT_TEST_CASE(ModuleAbiRejectsInvalidMagic) {
  uint8_t data[ModuleAbi::kHeaderSize];
  ModuleAbi::WriteHeader(data, /*manifest_hash=*/0);
  data[0] = 'X';

  ModuleAbiHeader header;
  EXPECT_STREQ("invalid module ABI magic",
               ModuleAbi::ReadHeader(data, &header));
}

UNIT_TEST_CASE(ModuleAbiRejectsUnsupportedVersion) {
  uint8_t data[ModuleAbi::kHeaderSize];
  ModuleAbi::WriteHeader(data, /*manifest_hash=*/0);
  data[4] = static_cast<uint8_t>(ModuleAbi::kCurrentFormatVersion + 1);
  data[5] = 0;

  ModuleAbiHeader header;
  EXPECT_STREQ("unsupported module ABI version",
               ModuleAbi::ReadHeader(data, &header));
}

UNIT_TEST_CASE(ModuleAbiRejectsSmallHeader) {
  uint8_t data[ModuleAbi::kHeaderSize];
  ModuleAbi::WriteHeader(data, /*manifest_hash=*/0);
  data[8] = static_cast<uint8_t>(ModuleAbi::kHeaderSize - 1);
  data[9] = 0;
  data[10] = 0;
  data[11] = 0;

  ModuleAbiHeader header;
  EXPECT_STREQ("invalid module ABI header size",
               ModuleAbi::ReadHeader(data, &header));
}

UNIT_TEST_CASE(ModuleExportTableLayout) {
  EXPECT_EQ(static_cast<intptr_t>(0),
            static_cast<intptr_t>(ModuleExportKind::kFunction));
  EXPECT_EQ(static_cast<intptr_t>(1),
            static_cast<intptr_t>(ModuleExportKind::kField));
  EXPECT_EQ(static_cast<intptr_t>(2),
            static_cast<intptr_t>(ModuleExportKind::kGetter));

  EXPECT_EQ(static_cast<intptr_t>(0), ModuleExportTable::kKindIndex);
  EXPECT_EQ(static_cast<intptr_t>(1), ModuleExportTable::kNameIndex);
  EXPECT_EQ(static_cast<intptr_t>(2), ModuleExportTable::kTargetIndex);
  EXPECT_EQ(static_cast<intptr_t>(3), ModuleExportTable::kEntryLength);
}

}  // namespace dart

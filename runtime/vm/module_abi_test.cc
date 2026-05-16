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
  ModuleAbi::WriteHeader(data, kTestManifestHash, /*payload_size=*/3);

  ModuleAbiHeader header;
  EXPECT_NULLPTR(ModuleAbi::ReadHeader(data, &header));
  EXPECT_EQ(ModuleAbi::kCurrentFormatVersion, header.format_version);
  EXPECT_EQ(static_cast<uint32_t>(ModuleAbi::kHeaderSize), header.header_size);
  EXPECT_EQ(static_cast<uint32_t>(3), header.payload_size);
  EXPECT_EQ(kTestManifestHash, header.manifest_hash);
  EXPECT_NE(static_cast<uint64_t>(0), header.sdk_hash);
  EXPECT_NE(static_cast<uint64_t>(0), header.compiler_flags_hash);
  EXPECT_NE(static_cast<uint16_t>(ModuleAbi::kTargetArchUnknown),
            header.target_arch);
  EXPECT_NE(static_cast<uint16_t>(ModuleAbi::kTargetOsUnknown),
            header.target_os);
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

UNIT_TEST_CASE(ModuleAbiRejectsIncompatibleManifestHash) {
  uint8_t data[ModuleAbi::kHeaderSize];
  ModuleAbi::WriteHeader(data, kTestManifestHash);

  ModuleAbiHeader header;
  EXPECT_NULLPTR(ModuleAbi::ReadHeader(data, &header));
  EXPECT_STREQ("module ABI manifest hash does not match host",
               ModuleAbi::ValidateCompatibility(
                   header, kTestManifestHash + 1, /*isolate_group=*/nullptr));
}

UNIT_TEST_CASE(ModuleAbiRejectsMissingHostManifestHash) {
  uint8_t data[ModuleAbi::kHeaderSize];
  ModuleAbi::WriteHeader(data, kTestManifestHash);

  ModuleAbiHeader header;
  EXPECT_NULLPTR(ModuleAbi::ReadHeader(data, &header));
  EXPECT_STREQ("module requires an ABI manifest hash but host has none",
               ModuleAbi::ValidateCompatibility(
                   header, /*host_manifest_hash=*/0,
                   /*isolate_group=*/nullptr));
}

UNIT_TEST_CASE(ModuleAbiRejectsIncompatibleSdkHash) {
  uint8_t data[ModuleAbi::kHeaderSize];
  ModuleAbi::WriteHeader(data, kTestManifestHash);

  ModuleAbiHeader header;
  EXPECT_NULLPTR(ModuleAbi::ReadHeader(data, &header));
  header.sdk_hash ^= 1;
  EXPECT_STREQ("module SDK hash does not match current SDK",
               ModuleAbi::ValidateCompatibility(
                   header, kTestManifestHash, /*isolate_group=*/nullptr));
}

UNIT_TEST_CASE(ModuleAbiRejectsIncompatibleTargetArch) {
  uint8_t data[ModuleAbi::kHeaderSize];
  ModuleAbi::WriteHeader(data, kTestManifestHash);

  ModuleAbiHeader header;
  EXPECT_NULLPTR(ModuleAbi::ReadHeader(data, &header));
  header.target_arch = ModuleAbi::kTargetArchUnknown;
  EXPECT_STREQ("module target architecture does not match current VM",
               ModuleAbi::ValidateCompatibility(
                   header, kTestManifestHash, /*isolate_group=*/nullptr));
}

UNIT_TEST_CASE(ModuleAbiRejectsIncompatibleCompilerFlags) {
  uint8_t data[ModuleAbi::kHeaderSize];
  ModuleAbi::WriteHeader(data, kTestManifestHash);

  ModuleAbiHeader header;
  EXPECT_NULLPTR(ModuleAbi::ReadHeader(data, &header));
  header.compiler_flags_hash ^= 1;
  EXPECT_STREQ("module compiler flags do not match current VM",
               ModuleAbi::ValidateCompatibility(
                   header, kTestManifestHash, /*isolate_group=*/nullptr));
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

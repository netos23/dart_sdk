// Copyright (c) 2026, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/module_abi.h"

#include "platform/assert.h"
#include "vm/unit_test.h"

namespace dart {
namespace {
static constexpr uint64_t kTestManifestHash = 0x1122334455667788;

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

void WriteRuntimeIds(uint8_t* data, const ModuleAbiRuntimeIds& runtime_ids) {
  WriteUint32LE(data, runtime_ids.private_class_count);
  WriteUint32LE(data + 4, runtime_ids.private_selector_count);
  WriteUint32LE(data + 8, runtime_ids.dispatch_table_entry_count);
  WriteUint32LE(data + 12, runtime_ids.reserved);
}

void WriteSectionHeader(uint8_t* data,
                        ModuleAbiPayloadSectionKind kind,
                        uint32_t entry_size,
                        uint32_t entry_count,
                        uint32_t payload_size) {
  WriteUint16LE(data, static_cast<uint16_t>(kind));
  WriteUint16LE(data + 2, 0);
  WriteUint32LE(data + 4, entry_size);
  WriteUint32LE(data + 8, entry_count);
  WriteUint32LE(data + 12, payload_size);
}

void WriteImportRecord(uint8_t* data,
                       ModuleAbiObjectKind object_kind,
                       uint32_t manifest_id,
                       uint64_t expected_hash) {
  WriteUint16LE(data, static_cast<uint16_t>(object_kind));
  WriteUint16LE(data + 2, 0);
  WriteUint32LE(data + 4, manifest_id);
  WriteUint64LE(data + 8, expected_hash);
}

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

UNIT_TEST_CASE(ModuleAbiReadRuntimeIds) {
  uint8_t data[ModuleAbi::kHeaderSize + ModuleAbi::kRuntimeIdsSize];
  ModuleAbiRuntimeIds runtime_ids;
  runtime_ids.private_class_count = 7;
  runtime_ids.private_selector_count = 3;
  runtime_ids.dispatch_table_entry_count = 19;
  ModuleAbi::WriteHeaderAndRuntimeIds(data, kTestManifestHash, runtime_ids);

  ModuleAbiHeader header;
  EXPECT_NULLPTR(ModuleAbi::ReadHeader(data, &header));
  EXPECT_EQ(static_cast<uint32_t>(ModuleAbi::kRuntimeIdsSize),
            header.payload_size);
  EXPECT_EQ(ModuleAbi::kHeaderSize + ModuleAbi::kRuntimeIdsSize,
            header.TotalSize());

  ModuleAbiRuntimeIds decoded;
  EXPECT_NULLPTR(ModuleAbi::ReadRuntimeIds(data, header, &decoded));
  EXPECT_EQ(static_cast<uint32_t>(7), decoded.private_class_count);
  EXPECT_EQ(static_cast<uint32_t>(3), decoded.private_selector_count);
  EXPECT_EQ(static_cast<uint32_t>(19), decoded.dispatch_table_entry_count);
  EXPECT_EQ(static_cast<uint32_t>(0), decoded.reserved);
}

UNIT_TEST_CASE(ModuleAbiReadRuntimeIdsAllowsNoPayload) {
  uint8_t data[ModuleAbi::kHeaderSize];
  ModuleAbi::WriteHeader(data, kTestManifestHash);

  ModuleAbiHeader header;
  EXPECT_NULLPTR(ModuleAbi::ReadHeader(data, &header));

  ModuleAbiRuntimeIds decoded;
  EXPECT_NULLPTR(ModuleAbi::ReadRuntimeIds(data, header, &decoded));
  EXPECT_EQ(static_cast<uint32_t>(0), decoded.private_class_count);
  EXPECT_EQ(static_cast<uint32_t>(0), decoded.private_selector_count);
  EXPECT_EQ(static_cast<uint32_t>(0), decoded.dispatch_table_entry_count);
  EXPECT_EQ(static_cast<uint32_t>(0), decoded.reserved);
}

UNIT_TEST_CASE(ModuleAbiReadsEmptyImportSection) {
  uint8_t data[ModuleAbi::kHeaderSize +
               ModuleAbi::kRuntimeIdsAndEmptyImportSectionSize];
  ModuleAbiRuntimeIds runtime_ids;
  runtime_ids.private_class_count = 2;
  ModuleAbi::WriteHeaderRuntimeIdsAndEmptyImportSection(data, kTestManifestHash,
                                                        runtime_ids);

  ModuleAbiHeader header;
  EXPECT_NULLPTR(ModuleAbi::ReadHeader(data, &header));
  EXPECT_EQ(
      static_cast<uint32_t>(ModuleAbi::kRuntimeIdsAndEmptyImportSectionSize),
      header.payload_size);
  EXPECT_NULLPTR(ModuleAbi::ValidatePayload(data, header));

  ModuleAbiPayloadSection section;
  EXPECT_NULLPTR(ModuleAbi::FindPayloadSection(
      data, header, ModuleAbiPayloadSectionKind::kAbiImports, &section));
  EXPECT(!section.IsNull());
  EXPECT_EQ(static_cast<uint16_t>(ModuleAbiPayloadSectionKind::kAbiImports),
            section.kind);
  EXPECT_EQ(static_cast<uint32_t>(ModuleAbi::kImportRecordSize),
            section.entry_size);
  EXPECT_EQ(static_cast<uint32_t>(0), section.entry_count);
  EXPECT_EQ(static_cast<uint32_t>(0), section.payload_size);
}

UNIT_TEST_CASE(ModuleAbiReadsImportRecord) {
  static constexpr uint32_t kManifestId = 42;
  static constexpr uint64_t kExpectedHash = 0x8877665544332211;
  static constexpr intptr_t kPayloadSize =
      ModuleAbi::kRuntimeIdsSize + ModuleAbi::kPayloadSectionHeaderSize +
      ModuleAbi::kImportRecordSize;
  uint8_t data[ModuleAbi::kHeaderSize + kPayloadSize];
  ModuleAbi::WriteHeader(data, kTestManifestHash, kPayloadSize);
  ModuleAbiRuntimeIds runtime_ids;
  WriteRuntimeIds(data + ModuleAbi::kHeaderSize, runtime_ids);
  uint8_t* section = data + ModuleAbi::kHeaderSize + ModuleAbi::kRuntimeIdsSize;
  WriteSectionHeader(section, ModuleAbiPayloadSectionKind::kAbiImports,
                     ModuleAbi::kImportRecordSize, /*entry_count=*/1,
                     ModuleAbi::kImportRecordSize);
  WriteImportRecord(section + ModuleAbi::kPayloadSectionHeaderSize,
                    ModuleAbiObjectKind::kClass, kManifestId, kExpectedHash);

  ModuleAbiHeader header;
  EXPECT_NULLPTR(ModuleAbi::ReadHeader(data, &header));
  EXPECT_NULLPTR(ModuleAbi::ValidatePayload(data, header));

  ModuleAbiPayloadSection decoded_section;
  EXPECT_NULLPTR(ModuleAbi::FindPayloadSection(
      data, header, ModuleAbiPayloadSectionKind::kAbiImports,
      &decoded_section));
  ModuleAbiImportRecord record;
  EXPECT_NULLPTR(ModuleAbi::ReadImportRecord(decoded_section, 0, &record));
  EXPECT_EQ(static_cast<uint16_t>(ModuleAbiObjectKind::kClass),
            record.object_kind);
  EXPECT_EQ(static_cast<uint32_t>(kManifestId), record.manifest_id);
  EXPECT_EQ(static_cast<uint64_t>(kExpectedHash), record.expected_hash);
}

UNIT_TEST_CASE(ModuleAbiRejectsInvalidImportRecord) {
  static constexpr intptr_t kPayloadSize =
      ModuleAbi::kRuntimeIdsSize + ModuleAbi::kPayloadSectionHeaderSize +
      ModuleAbi::kImportRecordSize;
  uint8_t data[ModuleAbi::kHeaderSize + kPayloadSize];
  ModuleAbi::WriteHeader(data, kTestManifestHash, kPayloadSize);
  ModuleAbiRuntimeIds runtime_ids;
  WriteRuntimeIds(data + ModuleAbi::kHeaderSize, runtime_ids);
  uint8_t* section = data + ModuleAbi::kHeaderSize + ModuleAbi::kRuntimeIdsSize;
  WriteSectionHeader(section, ModuleAbiPayloadSectionKind::kAbiImports,
                     ModuleAbi::kImportRecordSize, /*entry_count=*/1,
                     ModuleAbi::kImportRecordSize);
  WriteUint16LE(section + ModuleAbi::kPayloadSectionHeaderSize, 999);

  ModuleAbiHeader header;
  EXPECT_NULLPTR(ModuleAbi::ReadHeader(data, &header));
  EXPECT_STREQ("invalid module ABI import object kind",
               ModuleAbi::ValidatePayload(data, header));
}

UNIT_TEST_CASE(ModuleAbiRejectsShortRuntimeIdsPayload) {
  uint8_t data[ModuleAbi::kHeaderSize];
  ModuleAbi::WriteHeader(data, kTestManifestHash, /*payload_size=*/1);

  ModuleAbiHeader header;
  EXPECT_NULLPTR(ModuleAbi::ReadHeader(data, &header));
  ModuleAbiRuntimeIds decoded;
  EXPECT_STREQ("invalid module ABI runtime-id payload size",
               ModuleAbi::ReadRuntimeIds(data, header, &decoded));
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
               ModuleAbi::ValidateCompatibility(header, kTestManifestHash + 1,
                                                /*isolate_group=*/nullptr));
}

UNIT_TEST_CASE(ModuleAbiRejectsMissingHostManifestHash) {
  uint8_t data[ModuleAbi::kHeaderSize];
  ModuleAbi::WriteHeader(data, kTestManifestHash);

  ModuleAbiHeader header;
  EXPECT_NULLPTR(ModuleAbi::ReadHeader(data, &header));
  EXPECT_STREQ(
      "module requires an ABI manifest hash but host has none",
      ModuleAbi::ValidateCompatibility(header, /*host_manifest_hash=*/0,
                                       /*isolate_group=*/nullptr));
}

UNIT_TEST_CASE(ModuleAbiRejectsIncompatibleSdkHash) {
  uint8_t data[ModuleAbi::kHeaderSize];
  ModuleAbi::WriteHeader(data, kTestManifestHash);

  ModuleAbiHeader header;
  EXPECT_NULLPTR(ModuleAbi::ReadHeader(data, &header));
  header.sdk_hash ^= 1;
  EXPECT_STREQ("module SDK hash does not match current SDK",
               ModuleAbi::ValidateCompatibility(header, kTestManifestHash,
                                                /*isolate_group=*/nullptr));
}

UNIT_TEST_CASE(ModuleAbiRejectsIncompatibleTargetArch) {
  uint8_t data[ModuleAbi::kHeaderSize];
  ModuleAbi::WriteHeader(data, kTestManifestHash);

  ModuleAbiHeader header;
  EXPECT_NULLPTR(ModuleAbi::ReadHeader(data, &header));
  header.target_arch = ModuleAbi::kTargetArchUnknown;
  EXPECT_STREQ("module target architecture does not match current VM",
               ModuleAbi::ValidateCompatibility(header, kTestManifestHash,
                                                /*isolate_group=*/nullptr));
}

UNIT_TEST_CASE(ModuleAbiRejectsIncompatibleCompilerFlags) {
  uint8_t data[ModuleAbi::kHeaderSize];
  ModuleAbi::WriteHeader(data, kTestManifestHash);

  ModuleAbiHeader header;
  EXPECT_NULLPTR(ModuleAbi::ReadHeader(data, &header));
  header.compiler_flags_hash ^= 1;
  EXPECT_STREQ("module compiler flags do not match current VM",
               ModuleAbi::ValidateCompatibility(header, kTestManifestHash,
                                                /*isolate_group=*/nullptr));
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

UNIT_TEST_CASE(ModuleAbiImportTableLayout) {
  EXPECT_EQ(static_cast<intptr_t>(0), ModuleAbiImportTable::kKindIndex);
  EXPECT_EQ(static_cast<intptr_t>(1), ModuleAbiImportTable::kManifestIdIndex);
  EXPECT_EQ(static_cast<intptr_t>(2), ModuleAbiImportTable::kExpectedHashIndex);
  EXPECT_EQ(static_cast<intptr_t>(3),
            ModuleAbiImportTable::kResolvedObjectIndex);
  EXPECT_EQ(static_cast<intptr_t>(4), ModuleAbiImportTable::kEntryLength);
}

UNIT_TEST_CASE(ModuleAbiManifestTableLayout) {
  EXPECT_EQ(static_cast<intptr_t>(0), ModuleAbiManifestTable::kRawJsonIndex);
  EXPECT_EQ(static_cast<intptr_t>(1), ModuleAbiManifestTable::kLibraryIndex);
  EXPECT_EQ(static_cast<intptr_t>(2), ModuleAbiManifestTable::kClassIndex);
  EXPECT_EQ(static_cast<intptr_t>(3), ModuleAbiManifestTable::kFieldIndex);
  EXPECT_EQ(static_cast<intptr_t>(4), ModuleAbiManifestTable::kFunctionIndex);
  EXPECT_EQ(static_cast<intptr_t>(5), ModuleAbiManifestTable::kSelectorIndex);
  EXPECT_EQ(static_cast<intptr_t>(6), ModuleAbiManifestTable::kTypeIndex);
  EXPECT_EQ(static_cast<intptr_t>(7),
            ModuleAbiManifestTable::kTypeArgumentsIndex);
  EXPECT_EQ(static_cast<intptr_t>(8), ModuleAbiManifestTable::kLength);

  EXPECT_EQ(ModuleAbiManifestTable::kLibraryIndex,
            ModuleAbiManifestTable::ObjectKindToTableIndex(
                static_cast<uint16_t>(ModuleAbiObjectKind::kLibrary)));
  EXPECT_EQ(ModuleAbiManifestTable::kTypeArgumentsIndex,
            ModuleAbiManifestTable::ObjectKindToTableIndex(
                static_cast<uint16_t>(ModuleAbiObjectKind::kTypeArguments)));
  EXPECT_EQ(static_cast<intptr_t>(-1),
            ModuleAbiManifestTable::ObjectKindToTableIndex(999));
}

}  // namespace dart

/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

/// Writing encrypted tables.

#include <filesystem>
#include <fstream>

#include <arrow/c/bridge.h>
#include <arrow/json/from_string.h>
#include <arrow/type.h>
#include <gtest/gtest.h>

#include "iceberg/avro/avro_register.h"
#include "iceberg/catalog/memory/in_memory_catalog.h"
#include "iceberg/data/data_writer.h"
#include "iceberg/data/position_delete_writer.h"
#include "iceberg/deletes/dv_util_internal.h"
#include "iceberg/deletes/dv_writer.h"
#include "iceberg/encryption/encryption_register.h"
#include "iceberg/encryption/encryption_util.h"
#include "iceberg/file_reader.h"
#include "iceberg/manifest/manifest_entry.h"
#include "iceberg/manifest/manifest_reader.h"
#include "iceberg/manifest/manifest_writer.h"
#include "iceberg/parquet/parquet_register.h"
#include "iceberg/partition_spec.h"
#include "iceberg/schema.h"
#include "iceberg/schema_internal.h"
#include "iceberg/snapshot.h"
#include "iceberg/sort_order.h"
#include "iceberg/table.h"
#include "iceberg/table_metadata.h"
#include "iceberg/table_scan.h"
#include "iceberg/test/encryption_fixture.h"
#include "iceberg/test/matchers.h"
#include "iceberg/test/temp_file_test_base.h"
#include "iceberg/update/fast_append.h"
#include "iceberg/update/update_properties.h"

namespace iceberg {

class EncryptionWriteTest : public TempFileTestBase {
 protected:
  static void SetUpTestSuite() {
    avro::RegisterAll();
    parquet::RegisterAll();
  }

  std::shared_ptr<Schema> schema_ = std::make_shared<Schema>(
      std::vector<SchemaField>{SchemaField::MakeRequired(1, "id", int64()),
                               SchemaField::MakeOptional(2, "data", string())},
      /*schema_id=*/0);
};

TEST_F(EncryptionWriteTest, CommitsAreBlockedUntilSupported) {
  // Snapshot updates refuse encrypted tables
  auto warehouse = CreateNewTempFilePath();
  std::filesystem::create_directories(warehouse + "/t/metadata");
  std::shared_ptr<FileIO> io(arrow::MakeLocalFileIO());
  ICEBERG_UNWRAP_OR_FAIL(auto catalog, InMemoryCatalog::Make("c", io, warehouse, {}));
  TableIdentifier ident{.name = "t"};
  ICEBERG_UNWRAP_OR_FAIL(
      auto table, catalog->CreateTable(ident, schema_, PartitionSpec::Unpartitioned(),
                                       SortOrder::Unsorted(), warehouse + "/t", {}));
  ICEBERG_UNWRAP_OR_FAIL(auto props, table->NewUpdateProperties());
  props->Set("format-version", "3");
  props->Set("encryption.key-id", "keyA");
  ASSERT_THAT(props->Commit(), IsOk());
  ICEBERG_UNWRAP_OR_FAIL(table, catalog->LoadTable(ident));

  ICEBERG_UNWRAP_OR_FAIL(auto append, table->NewFastAppend());
  EXPECT_THAT(append->Commit(), IsError(ErrorKind::kNotSupported));
}

TEST_F(EncryptionWriteTest, EncryptionPropertiesRequireV3) {
  auto spec = PartitionSpec::Unpartitioned();
  auto order = SortOrder::Unsorted();
  EXPECT_THAT(TableMetadata::Make(*schema_, *spec, *order, "/t",
                                  {{"encryption.key-id", "keyA"}}, /*format_version=*/2),
              IsError(ErrorKind::kValidationFailed));
  ICEBERG_UNWRAP_OR_FAIL(auto metadata,
                         TableMetadata::Make(*schema_, *spec, *order, "/t",
                                             {{"encryption.key-id", "keyA"}}, 3));
  EXPECT_TRUE(EncryptionUtil::IsEncrypted(*metadata));

  // The table key ID cannot be changed or removed
  auto change = TableMetadataBuilder::BuildFrom(metadata.get());
  change->SetProperties({{"encryption.key-id", "keyB"}});
  EXPECT_THAT(change->Build(), IsError(ErrorKind::kValidationFailed));
  auto remove = TableMetadataBuilder::BuildFrom(metadata.get());
  remove->RemoveProperties({"encryption.key-id"});
  EXPECT_THAT(remove->Build(), IsError(ErrorKind::kValidationFailed));
  // Other properties can still change
  auto other = TableMetadataBuilder::BuildFrom(metadata.get());
  other->SetProperties({{"commit.retry.num-retries", "5"}});
  EXPECT_THAT(other->Build(), IsOk());
}

TEST_F(EncryptionWriteTest, MakeTableFileIO) {
  encryption::RegisterAll();
  ICEBERG_UNWRAP_OR_FAIL(auto fixture, JavaEncryptedTableFixture::Load(2));
  auto io = fixture.plain_io;

  // Encrypted table with a KMS: EncryptingFileIO
  ICEBERG_UNWRAP_OR_FAIL(
      auto table_io, EncryptionUtil::MakeTableFileIO(io, *fixture.metadata, fixture.kms));
  EXPECT_NE(EncryptingFileIO::From(table_io), nullptr);
  // Without a KMS: the plain FileIO (reads of encrypted files fail later)
  ICEBERG_UNWRAP_OR_FAIL(auto no_kms,
                         EncryptionUtil::MakeTableFileIO(io, *fixture.metadata, nullptr));
  EXPECT_EQ(no_kms, io);
  // A key ID from a trusted source must match the metadata
  EXPECT_THAT(EncryptionUtil::MakeTableFileIO(io, *fixture.metadata, fixture.kms,
                                              std::string("keyA")),
              IsOk());
  EXPECT_THAT(EncryptionUtil::MakeTableFileIO(io, *fixture.metadata, fixture.kms,
                                              std::string("keyB")),
              IsError(ErrorKind::kValidationFailed));
  // Unencrypted tables keep the plain FileIO
  ICEBERG_UNWRAP_OR_FAIL(auto plain_metadata,
                         TableMetadata::Make(*schema_, *PartitionSpec::Unpartitioned(),
                                             *SortOrder::Unsorted(), "/t", {}, 3));
  ICEBERG_UNWRAP_OR_FAIL(
      auto plain_io, EncryptionUtil::MakeTableFileIO(io, *plain_metadata, fixture.kms));
  EXPECT_EQ(plain_io, io);
}

TEST_F(EncryptionWriteTest, CatalogReadsEncryptedTable) {
  encryption::RegisterAll();
  InMemoryKms::Register();
  ICEBERG_UNWRAP_OR_FAIL(auto fixture, JavaEncryptedTableFixture::Load(4));
  // The catalog is configured with the KMS, as in Java: encryption.kms-impl
  ICEBERG_UNWRAP_OR_FAIL(
      auto catalog, InMemoryCatalog::Make("c", fixture.plain_io, CreateNewTempFilePath(),
                                          {{"encryption.kms-impl", "in-memory"},
                                           // Base64 of UnitestKMS's keyA
                                           {"encryption.in-memory-kms.key.keyA",
                                            "MDEyMzQ1Njc4OTAxMjM0NQ=="}}));
  TableIdentifier ident{.name = "java_table"};
  ICEBERG_UNWRAP_OR_FAIL(auto registered,
                         catalog->RegisterTable(ident, fixture.metadata_location));
  ICEBERG_UNWRAP_OR_FAIL(auto table, catalog->LoadTable(ident));
  ASSERT_NE(EncryptingFileIO::From(table->io()), nullptr);

  ICEBERG_UNWRAP_OR_FAIL(auto builder, table->NewScan());
  ICEBERG_UNWRAP_OR_FAIL(auto scan, builder->Build());
  ICEBERG_UNWRAP_OR_FAIL(auto tasks, scan->PlanFiles());
  EXPECT_EQ(tasks.size(), 3);

  // A catalog without a KMS can load the table, but not read it
  ICEBERG_UNWRAP_OR_FAIL(
      auto plain_catalog,
      InMemoryCatalog::Make("p", fixture.plain_io, CreateNewTempFilePath(), {}));
  ICEBERG_UNWRAP_OR_FAIL(auto plain_table,
                         plain_catalog->RegisterTable(ident, fixture.metadata_location));
  ICEBERG_UNWRAP_OR_FAIL(auto plain_builder, plain_table->NewScan());
  ICEBERG_UNWRAP_OR_FAIL(auto plain_scan, plain_builder->Build());
  EXPECT_THAT(plain_scan->PlanFiles(), IsError(ErrorKind::kNotSupported));
}

namespace {

std::string ReadMagic(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::string magic(4, '\0');
  in.read(magic.data(), 4);
  return magic;
}

}  // namespace

TEST_F(EncryptionWriteTest, WritesEncryptedManifestsAndManifestLists) {
  auto kms =
      std::make_shared<InMemoryKms>(std::unordered_map<std::string, std::vector<uint8_t>>{
          {"keyA", ReadEncryptionVector("master_key_keyA.bin")}});
  ICEBERG_UNWRAP_OR_FAIL(auto manager, StandardEncryptionManager::Make(
                                           {.table_key_id = "keyA", .kms = kms}));
  std::shared_ptr<FileIO> local_io(arrow::MakeLocalFileIO());
  auto io = std::make_shared<EncryptingFileIO>(local_io, manager);
  auto spec = PartitionSpec::Unpartitioned();

  // Manifest
  auto manifest_path = CreateNewTempFilePath();
  ICEBERG_UNWRAP_OR_FAIL(
      auto writer, ManifestWriter::MakeWriter(3, /*snapshot_id=*/1, manifest_path, io,
                                              spec, schema_, ManifestContent::kData,
                                              /*first_row_id=*/0));
  auto data_file = std::make_shared<DataFile>();
  data_file->file_path = "/data/file.parquet";
  data_file->file_format = FileFormatType::kParquet;
  data_file->record_count = 3;
  data_file->file_size_in_bytes = 1000;
  data_file->key_metadata = {1, 2, 3};
  ASSERT_THAT(writer->WriteAddedEntry(data_file), IsOk());
  ASSERT_THAT(writer->Close(), IsOk());
  ICEBERG_UNWRAP_OR_FAIL(auto manifest, writer->ToManifestFile());

  EXPECT_EQ(ReadMagic(manifest_path), "AGS1");
  auto stored = static_cast<int64_t>(std::filesystem::file_size(manifest_path));
  EXPECT_EQ(manifest.manifest_length, stored);
  ICEBERG_UNWRAP_OR_FAIL(auto key_metadata,
                         StandardKeyMetadata::Parse(manifest.key_metadata));
  EXPECT_EQ(key_metadata.file_length, stored);

  ICEBERG_UNWRAP_OR_FAIL(auto reader, ManifestReader::Make(manifest, io, schema_, spec));
  ICEBERG_UNWRAP_OR_FAIL(auto entries, reader->Entries());
  ASSERT_EQ(entries.size(), 1);
  EXPECT_EQ(entries[0].data_file->file_path, "/data/file.parquet");
  EXPECT_EQ(entries[0].data_file->key_metadata, (std::vector<uint8_t>{1, 2, 3}));
  // Not readable without the key
  EXPECT_THAT(ManifestReader::Make(manifest, local_io, schema_, spec),
              IsError(ErrorKind::kNotSupported));

  // Manifest list
  auto list_path = CreateNewTempFilePath();
  ICEBERG_UNWRAP_OR_FAIL(auto list_writer, ManifestListWriter::MakeWriter(
                                               3, /*snapshot_id=*/1, std::nullopt,
                                               list_path, io, /*sequence_number=*/1,
                                               /*first_row_id=*/0));
  ASSERT_THAT(list_writer->Add(manifest), IsOk());
  ASSERT_THAT(list_writer->Close(), IsOk());
  ICEBERG_UNWRAP_OR_FAIL(auto keys, list_writer->EncryptionKeys());
  ASSERT_TRUE(keys.has_value());
  EXPECT_EQ(ReadMagic(list_path), "AGS1");

  // A reader that only knows the committed keys
  ICEBERG_UNWRAP_OR_FAIL(
      auto reader_manager,
      StandardEncryptionManager::Make(
          {.table_key_id = "keyA",
           .encryption_keys = {keys->key_encryption_key, keys->file_key},
           .kms = kms}));
  auto reader_io = std::make_shared<EncryptingFileIO>(local_io, reader_manager);
  ICEBERG_UNWRAP_OR_FAIL(auto decrypted, reader_manager->DecryptManifestListKeyMetadata(
                                             keys->file_key.key_id));
  ICEBERG_UNWRAP_OR_FAIL(auto list_key_metadata, StandardKeyMetadata::Parse(decrypted));
  EXPECT_EQ(list_key_metadata.file_length,
            static_cast<int64_t>(std::filesystem::file_size(list_path)));

  Snapshot snapshot{.snapshot_id = 1,
                    .sequence_number = 1,
                    .manifest_list = list_path,
                    .key_id = keys->file_key.key_id};
  ICEBERG_UNWRAP_OR_FAIL(auto list_reader, ManifestListReader::Make(snapshot, reader_io));
  ICEBERG_UNWRAP_OR_FAIL(auto manifests, list_reader->Files());
  ASSERT_EQ(manifests.size(), 1);
  EXPECT_EQ(manifests[0].manifest_length, manifest.manifest_length);
  EXPECT_EQ(manifests[0].key_metadata, manifest.key_metadata);
}

TEST_F(EncryptionWriteTest, PlainManifestLengthIsUnchanged) {
  std::shared_ptr<FileIO> local_io(arrow::MakeLocalFileIO());
  auto manifest_path = CreateNewTempFilePath();
  ICEBERG_UNWRAP_OR_FAIL(
      auto writer, ManifestWriter::MakeWriter(2, 1, manifest_path, local_io,
                                              PartitionSpec::Unpartitioned(), schema_));
  auto data_file = std::make_shared<DataFile>();
  data_file->file_path = "/data/file.parquet";
  data_file->file_format = FileFormatType::kParquet;
  data_file->record_count = 3;
  data_file->file_size_in_bytes = 1000;
  ASSERT_THAT(writer->WriteAddedEntry(data_file), IsOk());
  ASSERT_THAT(writer->Close(), IsOk());
  ICEBERG_UNWRAP_OR_FAIL(auto manifest, writer->ToManifestFile());
  EXPECT_TRUE(manifest.key_metadata.empty());
  EXPECT_EQ(manifest.manifest_length,
            static_cast<int64_t>(std::filesystem::file_size(manifest_path)));
}

TEST_F(EncryptionWriteTest, WritesEncryptedDataFiles) {
  ICEBERG_UNWRAP_OR_FAIL(auto fixture, JavaEncryptedTableFixture::Load(2));
  ArrowSchema c_schema;
  ASSERT_THAT(ToArrowSchema(*schema_, &c_schema), IsOk());
  auto arrow_type =
      ::arrow::struct_(::arrow::ImportType(&c_schema).ValueOrDie()->fields());

  for (auto [format, magic] : {std::pair{FileFormatType::kParquet, "PARE"},
                               std::pair{FileFormatType::kAvro, "AGS1"}}) {
    auto path = CreateNewTempFilePath();
    ICEBERG_UNWRAP_OR_FAIL(auto writer,
                           DataWriter::Make({.path = path,
                                             .schema = schema_,
                                             .spec = PartitionSpec::Unpartitioned(),
                                             .partition = PartitionValues{},
                                             .format = format,
                                             .io = fixture.io}));
    auto array = ::arrow::json::ArrayFromJSONString(arrow_type,
                                                    R"([[1, "a"], [2, "b"], [3, "c"]])")
                     .ValueOrDie();
    ArrowArray c_array;
    ASSERT_TRUE(::arrow::ExportArray(*array, &c_array).ok());
    ASSERT_THAT(writer->Write(&c_array), IsOk());
    ASSERT_THAT(writer->Close(), IsOk());
    ICEBERG_UNWRAP_OR_FAIL(auto result, writer->Metadata());
    const auto& data_file = *result.data_files.front();

    EXPECT_EQ(ReadMagic(path), magic);
    auto stored = static_cast<int64_t>(std::filesystem::file_size(path));
    EXPECT_EQ(data_file.file_size_in_bytes, stored);
    ICEBERG_UNWRAP_OR_FAIL(auto key_metadata,
                           StandardKeyMetadata::Parse(data_file.key_metadata));
    EXPECT_EQ(key_metadata.file_length, stored);

    // Read it back as scans do: Parquet with the key metadata, Avro through the view
    ReaderOptions options{.path = path,
                          .length = static_cast<size_t>(stored),
                          .io = fixture.io,
                          .projection = schema_};
    if (format == FileFormatType::kParquet) {
      options.key_metadata = data_file.key_metadata;
    } else {
      ICEBERG_UNWRAP_OR_FAIL(
          options.io, EncryptingFileIO::ForFile(fixture.io, data_file.key_metadata));
    }
    ICEBERG_UNWRAP_OR_FAIL(auto reader, ReaderFactoryRegistry::Open(format, options));
    ICEBERG_UNWRAP_OR_FAIL(auto batch, reader->Next());
    ASSERT_TRUE(batch.has_value());
    EXPECT_EQ(batch->length, 3);
    if (batch->release != nullptr) {
      batch->release(&batch.value());
    }
  }
}

TEST_F(EncryptionWriteTest, WritesEncryptedPositionDeletes) {
  ICEBERG_UNWRAP_OR_FAIL(auto fixture, JavaEncryptedTableFixture::Load(2));
  auto path = CreateNewTempFilePath();
  ICEBERG_UNWRAP_OR_FAIL(
      auto writer, PositionDeleteWriter::Make({.path = path,
                                               .schema = schema_,
                                               .spec = PartitionSpec::Unpartitioned(),
                                               .partition = PartitionValues{},
                                               .io = fixture.io}));
  ASSERT_THAT(writer->WriteDelete("/data/file.parquet", 7), IsOk());
  ASSERT_THAT(writer->Close(), IsOk());
  ICEBERG_UNWRAP_OR_FAIL(auto result, writer->Metadata());
  const auto& delete_file = *result.data_files.front();
  EXPECT_EQ(ReadMagic(path), "PARE");
  ICEBERG_UNWRAP_OR_FAIL(auto key_metadata,
                         StandardKeyMetadata::Parse(delete_file.key_metadata));
  EXPECT_EQ(key_metadata.file_length,
            static_cast<int64_t>(std::filesystem::file_size(path)));
}

TEST_F(EncryptionWriteTest, WritesEncryptedDeletionVectors) {
  ICEBERG_UNWRAP_OR_FAIL(auto fixture, JavaEncryptedTableFixture::Load(2));
  auto path = CreateNewTempFilePath();
  ICEBERG_UNWRAP_OR_FAIL(
      auto writer, DVWriter::Make({.path = path,
                                   .io = fixture.io,
                                   .load_previous_deletes = [](std::string_view)
                                       -> Result<std::optional<PositionDeleteIndex>> {
                                     return std::nullopt;
                                   }}));
  auto spec = PartitionSpec::Unpartitioned();
  ASSERT_THAT(writer->Delete("/data/a.parquet", 3, spec, PartitionValues{}), IsOk());
  ASSERT_THAT(writer->Delete("/data/a.parquet", 5, spec, PartitionValues{}), IsOk());
  ASSERT_THAT(writer->Close(), IsOk());
  ICEBERG_UNWRAP_OR_FAIL(auto result, writer->Metadata());
  ASSERT_EQ(result.data_files.size(), 1);
  auto dv = result.data_files.front();

  EXPECT_EQ(ReadMagic(path), "AGS1");
  auto stored = static_cast<int64_t>(std::filesystem::file_size(path));
  EXPECT_EQ(dv->file_size_in_bytes, stored);
  ICEBERG_UNWRAP_OR_FAIL(auto key_metadata, StandardKeyMetadata::Parse(dv->key_metadata));
  EXPECT_EQ(key_metadata.file_length, stored);

  ICEBERG_UNWRAP_OR_FAIL(auto deletes, DVUtil::ReadDV(dv, fixture.io));
  EXPECT_EQ(deletes.Cardinality(), 2);
  EXPECT_TRUE(deletes.IsDeleted(3));
  EXPECT_TRUE(deletes.IsDeleted(5));
  EXPECT_FALSE(deletes.IsDeleted(4));
  // Not readable without the encryption manager
  EXPECT_THAT(DVUtil::ReadDV(dv, fixture.plain_io), IsError(ErrorKind::kNotSupported));
}

}  // namespace iceberg

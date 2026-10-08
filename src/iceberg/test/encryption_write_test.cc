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

#include <gtest/gtest.h>

#include "iceberg/avro/avro_register.h"
#include "iceberg/catalog/memory/in_memory_catalog.h"
#include "iceberg/data/data_writer.h"
#include "iceberg/encryption/encryption_register.h"
#include "iceberg/encryption/encryption_util.h"
#include "iceberg/parquet/parquet_register.h"
#include "iceberg/partition_spec.h"
#include "iceberg/schema.h"
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

TEST_F(EncryptionWriteTest, WritesAreBlockedUntilSupported) {
  ICEBERG_UNWRAP_OR_FAIL(auto fixture, JavaEncryptedTableFixture::Load(2));

  // Data writers refuse the FileIO of an encrypted table
  EXPECT_THAT(DataWriter::Make({.path = CreateNewTempFilePath(),
                                .schema = schema_,
                                .spec = PartitionSpec::Unpartitioned(),
                                .partition = PartitionValues{},
                                .format = FileFormatType::kParquet,
                                .io = fixture.io}),
              IsError(ErrorKind::kNotSupported));

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

}  // namespace iceberg

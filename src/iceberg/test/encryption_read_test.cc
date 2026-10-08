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

/// Reading tables encrypted by Java.

#include <filesystem>
#include <map>
#include <tuple>

#include <arrow/c/bridge.h>
#include <arrow/record_batch.h>
#include <arrow/scalar.h>
#include <arrow/table.h>
#include <gtest/gtest.h>

#include "iceberg/avro/avro_register.h"
#include "iceberg/data/file_scan_task_reader.h"
#include "iceberg/file_reader.h"
#include "iceberg/manifest/manifest_entry.h"
#include "iceberg/manifest/manifest_list.h"
#include "iceberg/manifest/manifest_reader.h"
#include "iceberg/parquet/parquet_register.h"
#include "iceberg/snapshot.h"
#include "iceberg/table.h"
#include "iceberg/table_scan.h"
#include "iceberg/test/encryption_fixture.h"
#include "iceberg/test/matchers.h"

namespace iceberg {

class EncryptionReadTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    avro::RegisterAll();
    parquet::RegisterAll();
  }

  static Result<std::vector<int64_t>> ScanIds(const Table& table) {
    return ScanTableIds(table);
  }

  static Result<std::shared_ptr<Table>> LoadTable(int version) {
    ICEBERG_ASSIGN_OR_RAISE(auto fixture, JavaEncryptedTableFixture::Load(version));
    return StaticTable::Make(TableIdentifier{.name = "t"}, fixture.metadata,
                             fixture.metadata_location, fixture.io);
  }
};

TEST_F(EncryptionReadTest, ReadsManifestLists) {
  ICEBERG_UNWRAP_OR_FAIL(auto fixture, JavaEncryptedTableFixture::Load(5));
  ASSERT_EQ(fixture.metadata->snapshots.size(), 4);

  for (const auto& snapshot : fixture.metadata->snapshots) {
    ICEBERG_UNWRAP_OR_FAIL(auto reader, ManifestListReader::Make(*snapshot, fixture.io));
    ICEBERG_UNWRAP_OR_FAIL(auto manifests, reader->Files());
    ASSERT_FALSE(manifests.empty());
    for (const auto& manifest : manifests) {
      // Manifests are encrypted: their key metadata includes the stored length, which
      // is also the recorded manifest length.
      ASSERT_FALSE(manifest.key_metadata.empty());
      ICEBERG_UNWRAP_OR_FAIL(auto key_metadata,
                             StandardKeyMetadata::Parse(manifest.key_metadata));
      auto name = std::filesystem::path(manifest.manifest_path).filename().string();
      auto stored = static_cast<int64_t>(std::filesystem::file_size(
          GetResourcePath("encryption/table/metadata/" + name)));
      EXPECT_EQ(key_metadata.file_length, stored);
      EXPECT_EQ(manifest.manifest_length, stored);
    }
  }

  // The latest snapshot sees all four manifests (3 data, 1 delete)
  SnapshotReader reader(fixture.metadata->snapshots.back().get());
  ICEBERG_UNWRAP_OR_FAIL(auto data_manifests, reader.DataManifests(fixture.io));
  ICEBERG_UNWRAP_OR_FAIL(auto delete_manifests, reader.DeleteManifests(fixture.io));
  EXPECT_EQ(data_manifests.size(), 3);
  EXPECT_EQ(delete_manifests.size(), 1);
}

TEST_F(EncryptionReadTest, ManifestListNeedsEncryption) {
  ICEBERG_UNWRAP_OR_FAIL(auto fixture, JavaEncryptedTableFixture::Load(5));
  const auto& snapshot = *fixture.metadata->snapshots.front();
  // Without the encryption manager the manifest list cannot be read
  EXPECT_THAT(ManifestListReader::Make(snapshot, fixture.plain_io),
              IsError(ErrorKind::kNotSupported));
  // Reading the AGS1 bytes as Avro fails cleanly
  EXPECT_THAT(ManifestListReader::Make(snapshot.manifest_list, fixture.plain_io),
              IsError(ErrorKind::kInvalid));
}

TEST_F(EncryptionReadTest, ReadsManifests) {
  ICEBERG_UNWRAP_OR_FAIL(auto fixture, JavaEncryptedTableFixture::Load(5));
  ICEBERG_UNWRAP_OR_FAIL(auto schema, fixture.metadata->Schema());
  SnapshotReader snapshot_reader(fixture.metadata->snapshots.back().get());
  ICEBERG_UNWRAP_OR_FAIL(auto manifests, snapshot_reader.Manifests(fixture.io));

  std::map<std::string, std::shared_ptr<DataFile>> files;
  for (const auto& manifest : manifests) {
    ICEBERG_UNWRAP_OR_FAIL(
        auto spec, fixture.metadata->PartitionSpecById(manifest.partition_spec_id));
    ICEBERG_UNWRAP_OR_FAIL(auto reader,
                           ManifestReader::Make(manifest, fixture.io, schema, spec));
    ICEBERG_UNWRAP_OR_FAIL(auto entries, reader->Entries());
    for (const auto& entry : entries) {
      files[std::filesystem::path(entry.data_file->file_path).filename().string()] =
          entry.data_file;
    }
  }
  ASSERT_EQ(files.size(), 4);

  // Every content file carries its own key metadata
  for (const auto& [name, file] : files) {
    ASSERT_FALSE(file->key_metadata.empty()) << name;
    ICEBERG_UNWRAP_OR_FAIL(auto key_metadata,
                           StandardKeyMetadata::Parse(file->key_metadata));
    EXPECT_EQ(key_metadata.encryption_key.size(), 16);
    EXPECT_EQ(key_metadata.aad_prefix->size(), 16);
    auto stored = static_cast<int64_t>(
        std::filesystem::file_size(GetResourcePath("encryption/table/data/" + name)));
    EXPECT_EQ(file->file_size_in_bytes, stored) << name;
    // Java records the stored length for every format. Parquet does not need it (it is
    // encrypted natively), AES GCM streams (Avro data, Puffin deletion vectors) do.
    EXPECT_EQ(key_metadata.file_length, stored) << name;
  }
  EXPECT_EQ(files.at("data-2.avro")->file_format, FileFormatType::kAvro);
}

TEST_F(EncryptionReadTest, PlansScans) {
  for (auto [version, data_files, delete_files] :
       {std::tuple{2, 1, 0}, std::tuple{4, 3, 0}, std::tuple{5, 3, 1}}) {
    ICEBERG_UNWRAP_OR_FAIL(auto fixture, JavaEncryptedTableFixture::Load(version));
    ICEBERG_UNWRAP_OR_FAIL(
        auto table, StaticTable::Make(TableIdentifier{.name = "t"}, fixture.metadata,
                                      fixture.metadata_location, fixture.io));
    ICEBERG_UNWRAP_OR_FAIL(auto builder, table->NewScan());
    ICEBERG_UNWRAP_OR_FAIL(auto scan, builder->Build());
    ICEBERG_UNWRAP_OR_FAIL(auto tasks, scan->PlanFiles());
    ASSERT_EQ(tasks.size(), data_files) << "v" << version;
    size_t deletes = 0;
    for (const auto& task : tasks) {
      deletes += task->delete_files().size();
    }
    EXPECT_EQ(deletes, delete_files) << "v" << version;
  }
}

TEST_F(EncryptionReadTest, ReadsDataFiles) {
  // v2: one Parquet file
  ICEBERG_UNWRAP_OR_FAIL(auto v2, LoadTable(2));
  ICEBERG_UNWRAP_OR_FAIL(auto v2_ids, ScanIds(*v2));
  EXPECT_EQ(v2_ids, (std::vector<int64_t>{0, 1, 2, 3, 4}));

  // v4: two Parquet files (modular encryption) and one Avro file (AES GCM stream)
  ICEBERG_UNWRAP_OR_FAIL(auto v4, LoadTable(4));
  ICEBERG_UNWRAP_OR_FAIL(auto v4_ids, ScanIds(*v4));
  EXPECT_EQ(v4_ids, (std::vector<int64_t>{0, 1, 2, 3, 4, 100, 101, 102, 103, 104, 200,
                                          201, 202}));
}

TEST_F(EncryptionReadTest, WrongKeyFailsCleanly) {
  ICEBERG_UNWRAP_OR_FAIL(auto fixture, JavaEncryptedTableFixture::Load(2));
  ICEBERG_UNWRAP_OR_FAIL(auto schema, fixture.metadata->Schema());
  ICEBERG_UNWRAP_OR_FAIL(auto table,
                         StaticTable::Make(TableIdentifier{.name = "t"}, fixture.metadata,
                                           fixture.metadata_location, fixture.io));
  ICEBERG_UNWRAP_OR_FAIL(auto builder, table->NewScan());
  ICEBERG_UNWRAP_OR_FAIL(auto scan, builder->Build());
  ICEBERG_UNWRAP_OR_FAIL(auto tasks, scan->PlanFiles());
  ASSERT_EQ(tasks.size(), 1);
  const auto& data_file = *tasks[0]->data_file();

  // The right key with another file's AAD prefix (a swapped file) is rejected
  ICEBERG_UNWRAP_OR_FAIL(auto key_metadata,
                         StandardKeyMetadata::Parse(data_file.key_metadata));
  key_metadata.aad_prefix->at(0) ^= 1;
  EXPECT_THAT(ReaderFactoryRegistry::Open(FileFormatType::kParquet,
                                          {.path = data_file.file_path,
                                           .io = fixture.io,
                                           .projection = schema,
                                           .key_metadata = key_metadata.Serialize()}),
              IsError(ErrorKind::kInvalid));
  // Without the key the encrypted footer cannot be read
  EXPECT_THAT(ReaderFactoryRegistry::Open(
                  FileFormatType::kParquet,
                  {.path = data_file.file_path, .io = fixture.io, .projection = schema}),
              IsError(ErrorKind::kInvalid));
}

TEST_F(EncryptionReadTest, ReadsDeletionVectors) {
  // v5: a Puffin deletion vector (an AES GCM stream) deletes id 1. Java's own generic
  // reader cannot read encrypted DVs yet (apache/iceberg#16157).
  ICEBERG_UNWRAP_OR_FAIL(auto v5, LoadTable(5));
  ICEBERG_UNWRAP_OR_FAIL(auto ids, ScanIds(*v5));
  EXPECT_EQ(ids,
            (std::vector<int64_t>{0, 2, 3, 4, 100, 101, 102, 103, 104, 200, 201, 202}));
}

}  // namespace iceberg

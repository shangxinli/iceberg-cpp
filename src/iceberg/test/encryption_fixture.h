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

#pragma once

/// \file iceberg/test/encryption_fixture.h
/// Access to the encrypted table written by Java (test/resources/encryption/table).
///
/// The fixture was written by Java's EncryptedTableFixtureGenerator at
/// /tmp/iceberg-cpp-encryption-fixture/encrypted_table, with master key "keyA" of
/// Java's UnitestKMS. Metadata versions:
/// - v2: Parquet append, ids 0-4 (data-0.parquet)
/// - v3: Parquet append, ids 100-104
/// - v4: Avro append, ids 200-202
/// - v5: deletion vector deleting id 1 (position 1 of data-0.parquet)

#include <memory>
#include <string>

#include "iceberg/arrow/arrow_io_util.h"
#include "iceberg/encryption/encrypting_file_io.h"
#include "iceberg/encryption/in_memory_kms.h"
#include "iceberg/encryption/standard_encryption_manager.h"
#include "iceberg/file_io.h"
#include "iceberg/table_metadata.h"
#include "iceberg/test/encryption_test_util.h"
#include "iceberg/test/test_resource.h"
#include "iceberg/util/macros.h"

namespace iceberg {

/// \brief A FileIO that maps a location prefix to another one.
class PrefixMappingFileIO : public FileIO {
 public:
  PrefixMappingFileIO(std::shared_ptr<FileIO> io, std::string from, std::string to)
      : io_(std::move(io)), from_(std::move(from)), to_(std::move(to)) {}

  Result<std::unique_ptr<InputFile>> NewInputFile(std::string location) override {
    return io_->NewInputFile(Map(location));
  }
  Result<std::unique_ptr<InputFile>> NewInputFile(std::string location,
                                                  size_t length) override {
    return io_->NewInputFile(Map(location), length);
  }
  Result<std::unique_ptr<OutputFile>> NewOutputFile(std::string location) override {
    return io_->NewOutputFile(Map(location));
  }
  Status DeleteFile(const std::string& location) override {
    return io_->DeleteFile(Map(location));
  }

 private:
  std::string Map(const std::string& location) const {
    return location.starts_with(from_) ? to_ + location.substr(from_.size()) : location;
  }

  std::shared_ptr<FileIO> io_;
  std::string from_;
  std::string to_;
};

/// \brief The Java fixture table: metadata, the KMS and an EncryptingFileIO over it.
struct JavaEncryptedTableFixture {
  static constexpr std::string_view kLocation =
      "/tmp/iceberg-cpp-encryption-fixture/encrypted_table";

  std::shared_ptr<TableMetadata> metadata;
  std::string metadata_location;
  std::shared_ptr<KeyManagementClient> kms;
  /// Maps fixture paths to the test resources; no encryption.
  std::shared_ptr<FileIO> plain_io;
  /// plain_io with the table's encryption manager.
  std::shared_ptr<FileIO> io;

  static Result<JavaEncryptedTableFixture> Load(int version) {
    JavaEncryptedTableFixture fixture;
    fixture.metadata_location = std::string(kLocation) + "/metadata/v" +
                                std::to_string(version) + ".metadata.json";
    ICEBERG_ASSIGN_OR_RAISE(
        auto metadata,
        ReadTableMetadataFromResource("encryption/table/metadata/v" +
                                      std::to_string(version) + ".metadata.json"));
    fixture.metadata = std::move(metadata);
    fixture.kms = std::make_shared<InMemoryKms>(
        std::unordered_map<std::string, std::vector<uint8_t>>{
            {"keyA", ReadEncryptionVector("master_key_keyA.bin")}});
    fixture.plain_io = std::make_shared<PrefixMappingFileIO>(
        std::shared_ptr<FileIO>(arrow::MakeLocalFileIO()), std::string(kLocation),
        GetResourcePath("encryption/table"));
    ICEBERG_ASSIGN_OR_RAISE(auto manager,
                            StandardEncryptionManager::Make(
                                {.table_key_id = fixture.metadata->properties.Get(
                                     TableProperties::kEncryptionTableKey),
                                 .encryption_keys = fixture.metadata->encryption_keys,
                                 .kms = fixture.kms}));
    fixture.io = std::make_shared<EncryptingFileIO>(fixture.plain_io, std::move(manager));
    return fixture;
  }
};

}  // namespace iceberg

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

#include "iceberg/encryption/encryption_util.h"

#include "iceberg/encryption/encrypting_file_io.h"
#include "iceberg/file_writer.h"
#include "iceberg/table_metadata.h"
#include "iceberg/table_properties.h"
#include "iceberg/util/macros.h"

namespace iceberg {

bool EncryptionUtil::IsEncrypted(const TableMetadata& metadata) {
  return !metadata.properties.Get(TableProperties::kEncryptionTableKey).empty();
}

Status EncryptionUtil::ValidateProperties(const TableMetadata* base,
                                          const TableMetadata& metadata) {
  const auto& configs = metadata.properties.configs();
  if (metadata.format_version < 3) {
    for (const auto& key : {TableProperties::kEncryptionTableKey.key(),
                            TableProperties::kEncryptionDekLength.key()}) {
      if (configs.contains(key)) {
        return ValidationFailed("Invalid property for v{}: {} requires format version 3",
                                metadata.format_version, key);
      }
    }
  }
  if (base != nullptr && IsEncrypted(*base)) {
    auto base_key = base->properties.Get(TableProperties::kEncryptionTableKey);
    auto new_key = metadata.properties.Get(TableProperties::kEncryptionTableKey);
    if (new_key.empty()) {
      return ValidationFailed("Cannot remove key ID from an encrypted table");
    }
    if (new_key != base_key) {
      return ValidationFailed("Cannot modify key ID of an encrypted table");
    }
  }
  return {};
}

Result<std::optional<StandardKeyMetadata>> EncryptionUtil::PrepareFileWrite(
    FileFormatType format, WriterOptions& options) {
  auto* encrypting_io = EncryptingFileIO::From(options.io);
  if (encrypting_io == nullptr) {
    return std::nullopt;
  }
  ICEBERG_ASSIGN_OR_RAISE(auto key_metadata,
                          encrypting_io->encryption()->NewKeyMetadata());
  if (format == FileFormatType::kParquet) {
    // Parquet modular encryption; the file is written through the plain FileIO.
    options.key_metadata = key_metadata.Serialize();
  } else {
    ICEBERG_ASSIGN_OR_RAISE(options.io, encrypting_io->ForEncryptedWrite(key_metadata));
  }
  return key_metadata;
}

std::vector<uint8_t> EncryptionUtil::FileKeyMetadata(
    const std::optional<StandardKeyMetadata>& key_metadata, int64_t stored_length) {
  if (!key_metadata.has_value()) {
    return {};
  }
  return key_metadata->WithFileLength(stored_length).Serialize();
}

Result<std::shared_ptr<FileIO>> EncryptionUtil::MakeTableFileIO(
    std::shared_ptr<FileIO> io, const TableMetadata& metadata,
    const std::shared_ptr<KeyManagementClient>& kms,
    const std::optional<std::string>& trusted_key_id) {
  auto metadata_key_id = metadata.properties.Get(TableProperties::kEncryptionTableKey);
  // The table key must not be taken from metadata.json alone when the catalog keeps a
  // trusted copy: an attacker could remove it to make writers produce plaintext files.
  if (trusted_key_id.has_value() && *trusted_key_id != metadata_key_id) {
    return ValidationFailed(
        "Table key ID in metadata ({}) does not match the catalog's ({}); the metadata "
        "file may have been tampered with",
        metadata_key_id, *trusted_key_id);
  }
  const auto& key_id = trusted_key_id.has_value() ? *trusted_key_id : metadata_key_id;
  if (key_id.empty() || kms == nullptr) {
    return io;
  }
  ICEBERG_ASSIGN_OR_RAISE(auto manager, EncryptionManagerRegistry::Make(
                                            {.table_key_id = key_id,
                                             .data_key_length = metadata.properties.Get(
                                                 TableProperties::kEncryptionDekLength),
                                             .encryption_keys = metadata.encryption_keys,
                                             .kms = kms}));
  return std::make_shared<EncryptingFileIO>(std::move(io), std::move(manager));
}

Status EncryptionUtil::CheckWriteSupported(const TableMetadata& metadata) {
  if (IsEncrypted(metadata)) {
    return NotSupported("Writing to encrypted tables is not supported yet");
  }
  return {};
}

}  // namespace iceberg

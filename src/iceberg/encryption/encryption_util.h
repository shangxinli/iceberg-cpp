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

/// \file iceberg/encryption/encryption_util.h
/// Table-level encryption policy.

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "iceberg/encryption/standard_key_metadata.h"
#include "iceberg/file_format.h"
#include "iceberg/iceberg_export.h"
#include "iceberg/result.h"
#include "iceberg/type_fwd.h"

namespace iceberg {

class FileIO;
class KeyManagementClient;
struct WriterOptions;

/// \brief Table-level encryption helpers.
struct ICEBERG_EXPORT EncryptionUtil {
  /// \brief Whether the table is encrypted (has the `encryption.key-id` property).
  static bool IsEncrypted(const TableMetadata& metadata);

  /// \brief Validate the encryption properties of new table metadata.
  ///
  /// Encryption properties require format version 3 or later, and the table key ID
  /// cannot be changed or removed once set.
  /// \param base the previous metadata, or nullptr for a new table
  static Status ValidateProperties(const TableMetadata* base,
                                   const TableMetadata& metadata);

  /// \brief Set up encryption for a new data or delete file.
  ///
  /// If `options.io` is the FileIO of an encrypted table, generates key metadata and
  /// configures the write: Parquet files are encrypted natively (options.key_metadata),
  /// other formats are written as AES GCM streams (options.io). Otherwise a no-op.
  ///
  /// \return the key metadata of the file, or nullopt if it is not encrypted
  static Result<std::optional<StandardKeyMetadata>> PrepareFileWrite(
      FileFormatType format, WriterOptions& options);

  /// \brief The key_metadata to record for a written file: its key metadata with the
  /// stored length (as Java), or empty if it is not encrypted.
  static std::vector<uint8_t> FileKeyMetadata(
      const std::optional<StandardKeyMetadata>& key_metadata, int64_t stored_length);

  /// \brief The FileIO of a table, for catalogs.
  ///
  /// Returns `io` for unencrypted tables, and an EncryptingFileIO over a new encryption
  /// manager for encrypted tables. If the catalog has no KMS client, returns `io`:
  /// reading encrypted files then fails with a clear error, and writing is rejected.
  ///
  /// \param io the catalog's FileIO for this table
  /// \param metadata the table metadata
  /// \param kms the catalog's KMS client, or nullptr
  /// \param trusted_key_id the table key ID from a trusted source (e.g. the metastore),
  /// when the catalog keeps one apart from metadata.json; it must match the metadata
  static Result<std::shared_ptr<FileIO>> MakeTableFileIO(
      std::shared_ptr<FileIO> io, const TableMetadata& metadata,
      const std::shared_ptr<KeyManagementClient>& kms,
      const std::optional<std::string>& trusted_key_id = std::nullopt);

  /// \brief Check that a commit to the table will write encrypted files.
  ///
  /// Fails if the table is encrypted but `io` is not an EncryptingFileIO (the catalog
  /// has no KMS client), which would write plaintext manifests into an encrypted table.
  static Status CheckCanWrite(const TableMetadata& metadata,
                              const std::shared_ptr<FileIO>& io);
};

}  // namespace iceberg

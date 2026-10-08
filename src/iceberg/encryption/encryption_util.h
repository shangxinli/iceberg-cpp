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

#include "iceberg/iceberg_export.h"
#include "iceberg/result.h"
#include "iceberg/type_fwd.h"

namespace iceberg {

class FileIO;
class KeyManagementClient;

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

  /// \brief Fails while writing to encrypted tables is not supported.
  ///
  /// TODO: remove once encrypted manifests, manifest lists and data files are written.
  static Status CheckWriteSupported(const TableMetadata& metadata);

  /// \brief Fails if `io` is the FileIO of an encrypted table and writing encrypted
  /// files is not supported yet.
  static Status CheckWriteSupported(const std::shared_ptr<FileIO>& io);
};

}  // namespace iceberg

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

/// \file iceberg/encryption/encrypting_file_io.h
/// FileIO that pairs a plain FileIO with a table's EncryptionManager.

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "iceberg/encryption/encryption_manager.h"
#include "iceberg/file_io.h"
#include "iceberg/iceberg_export.h"
#include "iceberg/result.h"

namespace iceberg {

/// \brief The FileIO of an encrypted table: a plain FileIO paired with the table's
/// EncryptionManager.
///
/// FileIO calls pass through to the plain FileIO unchanged, for files that are not
/// encrypted or that are encrypted natively by their format (Parquet). AES GCM Stream
/// files (manifests, manifest lists, Avro data files, Puffin files) are opened through a
/// per-file view, ForKeyMetadata() or ForEncryptedWrite(), which any reader or writer
/// that takes (FileIO, path) can use.
///
/// Mirrors Java's org.apache.iceberg.encryption.EncryptingFileIO.
class ICEBERG_EXPORT EncryptingFileIO : public FileIO {
 public:
  EncryptingFileIO(std::shared_ptr<FileIO> io, std::shared_ptr<EncryptionManager> em);

  /// \brief Return `io` itself when it is an EncryptingFileIO, or nullptr.
  static EncryptingFileIO* From(const std::shared_ptr<FileIO>& io);

  /// \brief The FileIO to read a file whose `key_metadata` is given (manifests, and
  /// AGS1-encrypted data files): `io` itself when key_metadata is empty, otherwise a
  /// decrypting view. Fails if the file is encrypted but `io` has no encryption.
  static Result<std::shared_ptr<FileIO>> ForFile(const std::shared_ptr<FileIO>& io,
                                                 std::span<const uint8_t> key_metadata);

  /// \brief The FileIO to read a snapshot's manifest list.
  static Result<std::shared_ptr<FileIO>> ForManifestList(
      const std::shared_ptr<FileIO>& io, const std::optional<std::string>& key_id);

  const std::shared_ptr<FileIO>& io() const { return io_; }
  const std::shared_ptr<EncryptionManager>& encryption() const { return em_; }

  /// \brief A view whose NewInputFile() decrypts AES GCM Stream files with the given
  /// serialized key metadata.
  Result<std::shared_ptr<FileIO>> ForKeyMetadata(
      std::span<const uint8_t> key_metadata) const;

  /// \brief A view whose NewOutputFile() writes AES GCM Stream files with the given
  /// key metadata (whose file length is not yet known).
  Result<std::shared_ptr<FileIO>> ForEncryptedWrite(
      const StandardKeyMetadata& key_metadata) const;

  Result<std::unique_ptr<InputFile>> NewInputFile(std::string file_location) override;
  Result<std::unique_ptr<InputFile>> NewInputFile(std::string file_location,
                                                  size_t length) override;
  Result<std::unique_ptr<OutputFile>> NewOutputFile(std::string file_location) override;
  Result<std::string> ReadFile(const std::string& file_location,
                               std::optional<size_t> length) override;
  Status WriteFile(const std::string& file_location, std::string_view content) override;
  Status DeleteFile(const std::string& file_location) override;
  Status DeleteFiles(const std::vector<std::string>& file_locations) override;
  SupportsStorageCredentials* AsSupportsStorageCredentials() override;

 private:
  std::shared_ptr<FileIO> io_;
  std::shared_ptr<EncryptionManager> em_;
};

}  // namespace iceberg

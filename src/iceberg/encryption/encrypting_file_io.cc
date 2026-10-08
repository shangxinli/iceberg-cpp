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

#include "iceberg/encryption/encrypting_file_io.h"

#include "iceberg/util/macros.h"

namespace iceberg {

namespace {

/// \brief Per-file view: reads decrypt with fixed key metadata, writes encrypt with it.
class KeyedFileIO : public FileIO {
 public:
  KeyedFileIO(std::shared_ptr<FileIO> io, std::shared_ptr<EncryptionManager> em,
              std::vector<uint8_t> read_key_metadata,
              std::optional<StandardKeyMetadata> write_key_metadata)
      : io_(std::move(io)),
        em_(std::move(em)),
        read_key_metadata_(std::move(read_key_metadata)),
        write_key_metadata_(std::move(write_key_metadata)) {}

  Result<std::unique_ptr<InputFile>> NewInputFile(std::string file_location) override {
    if (read_key_metadata_.empty()) {
      return NotSupported("Cannot read {}: no key metadata", file_location);
    }
    ICEBERG_ASSIGN_OR_RAISE(auto encrypted, io_->NewInputFile(std::move(file_location)));
    return em_->NewDecryptingInputFile(std::move(encrypted), read_key_metadata_);
  }

  // The length recorded in manifests is the stored length; the trusted length used for
  // truncation checks comes from the key metadata, so the passed length is not needed.
  Result<std::unique_ptr<InputFile>> NewInputFile(std::string file_location,
                                                  size_t length) override {
    if (read_key_metadata_.empty()) {
      return NotSupported("Cannot read {}: no key metadata", file_location);
    }
    ICEBERG_ASSIGN_OR_RAISE(auto encrypted,
                            io_->NewInputFile(std::move(file_location), length));
    return em_->NewDecryptingInputFile(std::move(encrypted), read_key_metadata_);
  }

  Result<std::unique_ptr<OutputFile>> NewOutputFile(std::string file_location) override {
    if (!write_key_metadata_.has_value()) {
      return NotSupported("Cannot write {}: no key metadata", file_location);
    }
    ICEBERG_ASSIGN_OR_RAISE(auto plain, io_->NewOutputFile(std::move(file_location)));
    return em_->NewEncryptingOutputFile(std::move(plain), *write_key_metadata_);
  }

  Status DeleteFile(const std::string& file_location) override {
    return io_->DeleteFile(file_location);
  }

 private:
  std::shared_ptr<FileIO> io_;
  std::shared_ptr<EncryptionManager> em_;
  std::vector<uint8_t> read_key_metadata_;
  std::optional<StandardKeyMetadata> write_key_metadata_;
};

}  // namespace

EncryptingFileIO::EncryptingFileIO(std::shared_ptr<FileIO> io,
                                   std::shared_ptr<EncryptionManager> em)
    : io_(std::move(io)), em_(std::move(em)) {
  // Never nest: wrap the innermost plain FileIO.
  if (auto* inner = From(io_)) {
    io_ = inner->io_;
  }
}

EncryptingFileIO* EncryptingFileIO::From(const std::shared_ptr<FileIO>& io) {
  return dynamic_cast<EncryptingFileIO*>(io.get());
}

Result<std::shared_ptr<FileIO>> EncryptingFileIO::ForFile(
    const std::shared_ptr<FileIO>& io, std::span<const uint8_t> key_metadata) {
  if (key_metadata.empty()) {
    return io;
  }
  auto* encrypting_io = From(io);
  if (encrypting_io == nullptr) {
    return NotSupported("Cannot read encrypted file: FileIO has no encryption manager");
  }
  return encrypting_io->ForKeyMetadata(key_metadata);
}

Result<std::shared_ptr<FileIO>> EncryptingFileIO::ForManifestList(
    const std::shared_ptr<FileIO>& io, const std::optional<std::string>& key_id) {
  if (!key_id.has_value()) {
    return io;
  }
  auto* encrypting_io = From(io);
  if (encrypting_io == nullptr) {
    return NotSupported(
        "Cannot read encrypted manifest list (key-id {}): FileIO has no encryption "
        "manager",
        *key_id);
  }
  ICEBERG_ASSIGN_OR_RAISE(auto key_metadata,
                          encrypting_io->em_->DecryptManifestListKeyMetadata(*key_id));
  return encrypting_io->ForKeyMetadata(key_metadata);
}

Result<std::shared_ptr<FileIO>> EncryptingFileIO::ForKeyMetadata(
    std::span<const uint8_t> key_metadata) const {
  ICEBERG_PRECHECK(!key_metadata.empty(), "Key metadata cannot be empty");
  return std::make_shared<KeyedFileIO>(
      io_, em_, std::vector<uint8_t>(key_metadata.begin(), key_metadata.end()),
      std::nullopt);
}

Result<std::shared_ptr<FileIO>> EncryptingFileIO::ForEncryptedWrite(
    const StandardKeyMetadata& key_metadata) const {
  return std::make_shared<KeyedFileIO>(io_, em_, std::vector<uint8_t>{}, key_metadata);
}

Result<std::unique_ptr<InputFile>> EncryptingFileIO::NewInputFile(
    std::string file_location) {
  return io_->NewInputFile(std::move(file_location));
}

Result<std::unique_ptr<InputFile>> EncryptingFileIO::NewInputFile(
    std::string file_location, size_t length) {
  return io_->NewInputFile(std::move(file_location), length);
}

Result<std::unique_ptr<OutputFile>> EncryptingFileIO::NewOutputFile(
    std::string file_location) {
  return io_->NewOutputFile(std::move(file_location));
}

Result<std::string> EncryptingFileIO::ReadFile(const std::string& file_location,
                                               std::optional<size_t> length) {
  return io_->ReadFile(file_location, length);
}

Status EncryptingFileIO::WriteFile(const std::string& file_location,
                                   std::string_view content) {
  return io_->WriteFile(file_location, content);
}

Status EncryptingFileIO::DeleteFile(const std::string& file_location) {
  return io_->DeleteFile(file_location);
}

Status EncryptingFileIO::DeleteFiles(const std::vector<std::string>& file_locations) {
  return io_->DeleteFiles(file_locations);
}

SupportsStorageCredentials* EncryptingFileIO::AsSupportsStorageCredentials() {
  return io_->AsSupportsStorageCredentials();
}

}  // namespace iceberg

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

#include <gtest/gtest.h>

#include "iceberg/test/matchers.h"
#include "iceberg/test/std_io.h"
#include "iceberg/test/temp_file_test_base.h"

namespace iceberg {

namespace {

/// Records how files are opened; "encrypts" by passing data through unchanged.
class RecordingEncryptionManager : public EncryptionManager {
 public:
  Result<StandardKeyMetadata> NewKeyMetadata() override {
    return StandardKeyMetadata{.encryption_key = std::vector<uint8_t>(16, 1)};
  }
  Result<std::unique_ptr<InputFile>> NewDecryptingInputFile(
      std::unique_ptr<InputFile> encrypted,
      std::span<const uint8_t> key_metadata) override {
    decrypted_with = std::vector<uint8_t>(key_metadata.begin(), key_metadata.end());
    return encrypted;
  }
  Result<std::unique_ptr<OutputFile>> NewEncryptingOutputFile(
      std::unique_ptr<OutputFile> plain,
      const StandardKeyMetadata& key_metadata) override {
    encrypted_with = key_metadata;
    return plain;
  }
  Result<std::vector<uint8_t>> DecryptManifestListKeyMetadata(
      std::string_view key_id) override {
    if (key_id != "list-key") {
      return Invalid("unknown key {}", key_id);
    }
    return std::vector<uint8_t>{7, 7, 7};
  }
  Result<ManifestListEncryptionKeys> RegisterManifestListKeyMetadata(
      const StandardKeyMetadata&) override {
    return NotImplemented("");
  }

  std::optional<std::vector<uint8_t>> decrypted_with;
  std::optional<StandardKeyMetadata> encrypted_with;
};

}  // namespace

class EncryptingFileIOTest : public TempFileTestBase {
 protected:
  void SetUp() override {
    TempFileTestBase::SetUp();
    plain_io_ = std::make_shared<test::StdFileIO>();
    em_ = std::make_shared<RecordingEncryptionManager>();
    io_ = std::make_shared<EncryptingFileIO>(plain_io_, em_);
  }

  std::shared_ptr<FileIO> plain_io_;
  std::shared_ptr<RecordingEncryptionManager> em_;
  std::shared_ptr<FileIO> io_;
};

TEST_F(EncryptingFileIOTest, PassesPlainCallsThrough) {
  auto path = CreateNewTempFilePath();
  ASSERT_THAT(io_->WriteFile(path, "hello"), IsOk());
  ICEBERG_UNWRAP_OR_FAIL(auto content, io_->ReadFile(path, std::nullopt));
  EXPECT_EQ(content, "hello");
  EXPECT_FALSE(em_->decrypted_with.has_value());
  EXPECT_FALSE(em_->encrypted_with.has_value());
  ASSERT_THAT(io_->DeleteFile(path), IsOk());
}

TEST_F(EncryptingFileIOTest, ForFile) {
  // Files without key metadata use the FileIO as is
  ICEBERG_UNWRAP_OR_FAIL(auto same, EncryptingFileIO::ForFile(io_, {}));
  EXPECT_EQ(same, io_);

  auto path = CreateNewTempFilePath();
  ASSERT_THAT(io_->WriteFile(path, "data"), IsOk());
  std::vector<uint8_t> key_metadata{1, 2, 3};
  ICEBERG_UNWRAP_OR_FAIL(auto view, EncryptingFileIO::ForFile(io_, key_metadata));
  ICEBERG_UNWRAP_OR_FAIL(auto content, view->ReadFile(path, std::nullopt));
  EXPECT_EQ(content, "data");
  EXPECT_EQ(em_->decrypted_with, key_metadata);

  // An encrypted file cannot be read without an encryption manager
  EXPECT_THAT(EncryptingFileIO::ForFile(plain_io_, key_metadata),
              IsError(ErrorKind::kNotSupported));
}

TEST_F(EncryptingFileIOTest, ForManifestList) {
  ICEBERG_UNWRAP_OR_FAIL(auto same, EncryptingFileIO::ForManifestList(io_, std::nullopt));
  EXPECT_EQ(same, io_);

  auto path = CreateNewTempFilePath();
  ASSERT_THAT(io_->WriteFile(path, "list"), IsOk());
  ICEBERG_UNWRAP_OR_FAIL(auto view, EncryptingFileIO::ForManifestList(io_, "list-key"));
  ICEBERG_UNWRAP_OR_FAIL(auto content, view->ReadFile(path, std::nullopt));
  EXPECT_EQ(content, "list");
  EXPECT_EQ(em_->decrypted_with, (std::vector<uint8_t>{7, 7, 7}));

  EXPECT_THAT(EncryptingFileIO::ForManifestList(io_, "other-key"),
              IsError(ErrorKind::kInvalid));
  EXPECT_THAT(EncryptingFileIO::ForManifestList(plain_io_, "list-key"),
              IsError(ErrorKind::kNotSupported));
}

TEST_F(EncryptingFileIOTest, ForEncryptedWrite) {
  auto* encrypting_io = EncryptingFileIO::From(io_);
  ASSERT_NE(encrypting_io, nullptr);
  EXPECT_EQ(EncryptingFileIO::From(plain_io_), nullptr);

  StandardKeyMetadata key_metadata{.encryption_key = std::vector<uint8_t>(16, 9)};
  ICEBERG_UNWRAP_OR_FAIL(auto view, encrypting_io->ForEncryptedWrite(key_metadata));
  auto path = CreateNewTempFilePath();
  ASSERT_THAT(view->WriteFile(path, "secret"), IsOk());
  EXPECT_EQ(em_->encrypted_with, key_metadata);

  // A write view cannot read, and a read view cannot write
  EXPECT_THAT(view->NewInputFile(path), IsError(ErrorKind::kNotSupported));
  ICEBERG_UNWRAP_OR_FAIL(auto read_view,
                         encrypting_io->ForKeyMetadata(key_metadata.Serialize()));
  EXPECT_THAT(read_view->NewOutputFile(path), IsError(ErrorKind::kNotSupported));
}

TEST_F(EncryptingFileIOTest, DoesNotNest) {
  auto nested = std::make_shared<EncryptingFileIO>(io_, em_);
  EXPECT_EQ(nested->io(), plain_io_);
}

}  // namespace iceberg

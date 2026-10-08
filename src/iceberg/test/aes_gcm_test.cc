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

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "iceberg/arrow/arrow_io_util.h"
#include "iceberg/encryption/aes_gcm_internal.h"
#include "iceberg/encryption/aes_gcm_stream.h"
#include "iceberg/encryption/standard_key_metadata.h"
#include "iceberg/test/encryption_test_util.h"
#include "iceberg/test/matchers.h"
#include "iceberg/test/temp_file_test_base.h"

namespace iceberg {

namespace {

using encryption::AesGcmCipher;

std::span<const uint8_t> AsBytes(std::string_view s) {
  return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

Result<std::vector<uint8_t>> ReadAll(InputFile& file) {
  ICEBERG_ASSIGN_OR_RAISE(auto size, file.Size());
  ICEBERG_ASSIGN_OR_RAISE(auto stream, file.Open());
  std::vector<uint8_t> out(size);
  if (size > 0) {
    ICEBERG_RETURN_UNEXPECTED(
        stream->ReadFully(0, std::as_writable_bytes(std::span(out))));
  }
  return out;
}

}  // namespace

TEST(AesGcmCipherTest, RoundTrip) {
  for (size_t key_length : {16, 24, 32}) {
    ICEBERG_UNWRAP_OR_FAIL(auto cipher, AesGcmCipher::Make(VectorBytes(key_length, 1)));
    auto plaintext = VectorBytes(100, 2);
    ICEBERG_UNWRAP_OR_FAIL(auto ciphertext, cipher->Encrypt(plaintext, AsBytes("aad")));
    EXPECT_EQ(ciphertext.size(), plaintext.size() + 28);
    ICEBERG_UNWRAP_OR_FAIL(auto decrypted, cipher->Decrypt(ciphertext, AsBytes("aad")));
    EXPECT_EQ(decrypted, plaintext);

    // Random nonces: encrypting twice gives different ciphertexts
    ICEBERG_UNWRAP_OR_FAIL(auto again, cipher->Encrypt(plaintext, AsBytes("aad")));
    EXPECT_NE(again, ciphertext);

    // Wrong AAD, tampered ciphertext and truncated ciphertext fail authentication
    EXPECT_THAT(cipher->Decrypt(ciphertext, AsBytes("other")),
                IsError(ErrorKind::kInvalid));
    ciphertext[20] ^= 1;
    EXPECT_THAT(cipher->Decrypt(ciphertext, AsBytes("aad")),
                IsError(ErrorKind::kInvalid));
    EXPECT_THAT(cipher->Decrypt(std::span(ciphertext).first(27), AsBytes("aad")),
                IsError(ErrorKind::kInvalid));
  }
  EXPECT_THAT(AesGcmCipher::Make(VectorBytes(15, 1)),
              IsError(ErrorKind::kInvalidArgument));
}

TEST(AesGcmCipherTest, DecryptsJavaManifestListKeyMetadata) {
  // StandardEncryptionManager encrypts manifest list key metadata with a key encryption
  // key, using the KEK timestamp as AAD.
  auto timestamp = ReadEncryptionVector("kek_timestamp.txt");
  ICEBERG_UNWRAP_OR_FAIL(auto cipher,
                         AesGcmCipher::Make(ReadEncryptionVector("kek.bin")));
  ICEBERG_UNWRAP_OR_FAIL(
      auto decrypted,
      cipher->Decrypt(ReadEncryptionVector("manifest_list_key_metadata_encrypted.bin"),
                      timestamp));
  ICEBERG_UNWRAP_OR_FAIL(auto metadata, StandardKeyMetadata::Parse(decrypted));
  EXPECT_EQ(metadata.encryption_key, VectorBytes(16, 1));
  EXPECT_EQ(metadata.aad_prefix, VectorBytes(16, 2));
  EXPECT_EQ(metadata.file_length, 4242);
}

class AesGcmStreamTest : public TempFileTestBase {
 protected:
  void SetUp() override {
    TempFileTestBase::SetUp();
    io_ = std::shared_ptr<FileIO>(arrow::MakeLocalFileIO());
  }

  // Write `plain` as an AGS1 stream and return the stored length.
  int64_t Write(const std::string& path, std::span<const uint8_t> plain,
                const std::vector<uint8_t>& key, const std::vector<uint8_t>& aad,
                size_t chunk = 1 << 20) {
    auto out = io_->NewOutputFile(path);
    EXPECT_THAT(out, IsOk());
    AesGcmOutputFile file(std::move(out.value()), key, aad);
    auto stream = file.CreateOrOverwrite();
    EXPECT_THAT(stream, IsOk());
    for (size_t pos = 0; pos < plain.size(); pos += chunk) {
      auto part = plain.subspan(pos, std::min(chunk, plain.size() - pos));
      EXPECT_THAT((*stream)->Write(std::as_bytes(part)), IsOk());
    }
    EXPECT_EQ((*stream)->Position().value(), static_cast<int64_t>(plain.size()));
    EXPECT_THAT((*stream)->Close(), IsOk());
    auto stored = (*stream)->StoredLength().value();
    EXPECT_EQ(stored, static_cast<int64_t>(std::filesystem::file_size(path)));
    return stored;
  }

  std::unique_ptr<AesGcmInputFile> Open(const std::string& path,
                                        const std::vector<uint8_t>& key,
                                        const std::vector<uint8_t>& aad, int64_t length) {
    auto in = io_->NewInputFile(path);
    EXPECT_THAT(in, IsOk());
    return std::make_unique<AesGcmInputFile>(std::move(in.value()), key, aad, length);
  }

  std::shared_ptr<FileIO> io_;
};

TEST_F(AesGcmStreamTest, ReadsJavaVectors) {
  auto key = ReadEncryptionVector("key.bin");
  auto aad = ReadEncryptionVector("aad_prefix.bin");
  for (size_t length : {0, 1, 100}) {
    auto name = "encryption/vectors/ags1_" + std::to_string(length) + ".bin";
    auto path = GetResourcePath(name);
    auto file = Open(path, key, aad, std::filesystem::file_size(path));
    ICEBERG_UNWRAP_OR_FAIL(auto plain, ReadAll(*file));
    EXPECT_EQ(plain, VectorBytes(length, 5)) << name;
  }
}

TEST_F(AesGcmStreamTest, WritesJavaCompatibleLayout) {
  // Same plaintext, key and AAD prefix as Java's ags1_100.bin: same header and length
  // (the ciphertext differs only by the random nonce).
  auto key = ReadEncryptionVector("key.bin");
  auto aad = ReadEncryptionVector("aad_prefix.bin");
  auto java = ReadEncryptionVector("ags1_100.bin");
  auto path = CreateNewTempFilePath();
  auto stored = Write(path, VectorBytes(100, 5), key, aad);
  ASSERT_EQ(stored, static_cast<int64_t>(java.size()));
  std::ifstream in(path, std::ios::binary);
  std::vector<uint8_t> header(8);
  in.read(reinterpret_cast<char*>(header.data()), 8);
  EXPECT_EQ(header, std::vector<uint8_t>(java.begin(), java.begin() + 8));
}

TEST_F(AesGcmStreamTest, RoundTripAcrossBlocks) {
  auto key = VectorBytes(32, 7);
  auto aad = VectorBytes(16, 9);
  constexpr size_t kBlock = AesGcmStream::kPlainBlockSize;
  for (size_t size :
       {size_t{0}, size_t{1}, kBlock - 1, kBlock, kBlock + 1, 2 * kBlock + 17}) {
    auto path = CreateNewTempFilePath();
    auto plain = VectorBytes(size, 5);
    // Odd write sizes exercise writes that straddle blocks
    int64_t stored = Write(path, plain, key, aad, /*chunk=*/333331);
    size_t blocks = size == 0 ? 1 : (size + kBlock - 1) / kBlock;
    EXPECT_EQ(stored, static_cast<int64_t>(8 + size + blocks * 28));
    EXPECT_EQ(AesGcmStream::PlaintextLength(stored), static_cast<int64_t>(size));

    auto file = Open(path, key, aad, stored);
    ICEBERG_UNWRAP_OR_FAIL(auto read, ReadAll(*file));
    EXPECT_EQ(read, plain);

    if (size > kBlock + 10) {
      // Random access across a block boundary
      ICEBERG_UNWRAP_OR_FAIL(auto stream, file->Open());
      std::vector<std::byte> window(10);
      ASSERT_THAT(stream->ReadFully(kBlock - 5, window), IsOk());
      EXPECT_EQ(std::memcmp(window.data(), plain.data() + kBlock - 5, 10), 0);
    }
  }
}

TEST_F(AesGcmStreamTest, DetectsTampering) {
  auto key = VectorBytes(16, 7);
  auto aad = VectorBytes(16, 9);
  auto path = CreateNewTempFilePath();
  auto plain = VectorBytes(AesGcmStream::kPlainBlockSize + 100, 5);
  int64_t stored = Write(path, plain, key, aad);

  // Wrong key or AAD prefix (a file swapped in from another manifest entry)
  EXPECT_THAT(ReadAll(*Open(path, VectorBytes(16, 8), aad, stored)),
              IsError(ErrorKind::kInvalid));
  EXPECT_THAT(ReadAll(*Open(path, key, VectorBytes(16, 10), stored)),
              IsError(ErrorKind::kInvalid));

  // Truncation: the trusted length from the key metadata no longer matches the file
  std::filesystem::resize_file(path, 8 + AesGcmStream::kCipherBlockSize);
  EXPECT_THAT(ReadAll(*Open(path, key, aad, stored)), IsError(ErrorKind::kIOError));
  // AGS1 blocks do not mark the end of the stream: a truncated file read with its own
  // (untrusted) length still decrypts. This is why readers must take the length from the
  // key metadata.
  auto truncated = Open(path, key, aad, 8 + AesGcmStream::kCipherBlockSize);
  ICEBERG_UNWRAP_OR_FAIL(auto first_block, ReadAll(*truncated));
  EXPECT_EQ(first_block.size(), AesGcmStream::kPlainBlockSize);

  // Bad magic
  std::filesystem::resize_file(path, 0);
  std::ofstream(path, std::ios::binary) << "XXXX0000" << std::string(28, '\0');
  EXPECT_THAT(Open(path, key, aad, 36)->Open(), IsError(ErrorKind::kInvalid));
  // Shorter than an empty stream
  EXPECT_THAT(Open(path, key, aad, 35)->Open(), IsError(ErrorKind::kInvalid));
}

}  // namespace iceberg

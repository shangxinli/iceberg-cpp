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

/// \file iceberg/kms/kms_clients.h
/// KMS clients of AWS KMS, Google Cloud KMS and Azure Key Vault.
///
/// The clients call the services' REST APIs directly (no cloud SDK). They read the
/// same catalog properties as their Java counterparts where Java defines one;
/// properties marked "C++ only" have no Java equivalent, because Java takes them from
/// the cloud SDK's default credential chain.

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "iceberg/encryption/key_management_client.h"
#include "iceberg/kms/iceberg_kms_export.h"
#include "iceberg/kms/kms_http_client.h"

namespace iceberg::kms {

using Properties = std::unordered_map<std::string, std::string>;

/// \brief AWS KMS client (Java: org.apache.iceberg.aws.AwsKeyManagementClient).
///
/// The wrapping key ID is a KMS key ID, ARN or alias. Properties:
/// - `client.region`: the AWS region (default: the AWS_REGION environment variable);
/// - `kms.endpoint` (C++ only): endpoint override, e.g. for LocalStack;
/// - `kms.encryption-algorithm-spec`: default SYMMETRIC_DEFAULT;
/// - `kms.data-key-spec`: key spec of GenerateKey, default AES_256;
/// - `kms.access-key-id`, `kms.secret-access-key`, `kms.session-token` (C++ only):
///   credentials; default: the AWS_ACCESS_KEY_ID, AWS_SECRET_ACCESS_KEY and
///   AWS_SESSION_TOKEN environment variables.
///
/// Requests are signed with AWS Signature Version 4.
class ICEBERG_KMS_EXPORT AwsKmsClient : public KeyManagementClient {
 public:
  static constexpr std::string_view kRegion = "client.region";
  static constexpr std::string_view kEndpoint = "kms.endpoint";
  static constexpr std::string_view kEncryptionAlgorithmSpec =
      "kms.encryption-algorithm-spec";
  static constexpr std::string_view kDataKeySpec = "kms.data-key-spec";
  static constexpr std::string_view kAccessKeyId = "kms.access-key-id";
  static constexpr std::string_view kSecretAccessKey = "kms.secret-access-key";
  static constexpr std::string_view kSessionToken = "kms.session-token";

  /// \brief Returns the current time as yyyyMMdd'T'HHmmss'Z'.
  using Clock = std::function<std::string()>;

  explicit AwsKmsClient(std::shared_ptr<HttpClient> http = nullptr, Clock clock = {});
  ~AwsKmsClient() override;

  Status Initialize(const Properties& properties) override;
  Result<std::vector<uint8_t>> WrapKey(std::span<const uint8_t> key,
                                       std::string_view wrapping_key_id) override;
  Result<std::vector<uint8_t>> UnwrapKey(std::span<const uint8_t> wrapped_key,
                                         std::string_view wrapping_key_id) override;
  bool SupportsKeyGeneration() const override { return true; }
  Result<GeneratedKey> GenerateKey(std::string_view wrapping_key_id) override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

/// \brief Google Cloud KMS client (Java: org.apache.iceberg.gcp.GcpKeyManagementClient).
///
/// The wrapping key ID is the key resource name,
/// `projects/*/locations/*/keyRings/*/cryptoKeys/*`. Properties:
/// - `gcs.oauth2.token`: OAuth2 access token (required; application default
///   credentials are not supported);
/// - `gcp.kms.endpoint` (C++ only): default https://cloudkms.googleapis.com.
class ICEBERG_KMS_EXPORT GcpKmsClient : public KeyManagementClient {
 public:
  static constexpr std::string_view kOAuth2Token = "gcs.oauth2.token";
  static constexpr std::string_view kEndpoint = "gcp.kms.endpoint";

  explicit GcpKmsClient(std::shared_ptr<HttpClient> http = nullptr);
  ~GcpKmsClient() override;

  Status Initialize(const Properties& properties) override;
  Result<std::vector<uint8_t>> WrapKey(std::span<const uint8_t> key,
                                       std::string_view wrapping_key_id) override;
  Result<std::vector<uint8_t>> UnwrapKey(std::span<const uint8_t> wrapped_key,
                                         std::string_view wrapping_key_id) override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

/// \brief Azure Key Vault client
/// (Java: org.apache.iceberg.azure.keymanagement.AzureKeyManagementClient).
///
/// The wrapping key ID is the key name, optionally followed by `/<version>`.
/// Properties:
/// - `azure.keyvault.url`: the vault URL, e.g. https://myvault.vault.azure.net
///   (required);
/// - `azure.keyvault.key-wrap-algorithm`: default RSA-OAEP-256;
/// - `azure.keyvault.token` (C++ only): Microsoft Entra access token (required).
class ICEBERG_KMS_EXPORT AzureKmsClient : public KeyManagementClient {
 public:
  static constexpr std::string_view kVaultUrl = "azure.keyvault.url";
  static constexpr std::string_view kKeyWrapAlgorithm =
      "azure.keyvault.key-wrap-algorithm";
  static constexpr std::string_view kToken = "azure.keyvault.token";

  explicit AzureKmsClient(std::shared_ptr<HttpClient> http = nullptr);
  ~AzureKmsClient() override;

  Status Initialize(const Properties& properties) override;
  Result<std::vector<uint8_t>> WrapKey(std::span<const uint8_t> key,
                                       std::string_view wrapping_key_id) override;
  Result<std::vector<uint8_t>> UnwrapKey(std::span<const uint8_t> wrapped_key,
                                         std::string_view wrapping_key_id) override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

/// \brief Register the clients in KmsRegistry as `aws`, `gcp` and `azure`.
///
/// Also done at static initialization when this library is linked.
ICEBERG_KMS_EXPORT void RegisterAll();

}  // namespace iceberg::kms

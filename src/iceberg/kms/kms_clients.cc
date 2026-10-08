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

#include "iceberg/kms/kms_clients.h"

#include <chrono>
#include <cstdlib>
#include <format>

#include <nlohmann/json.hpp>

#include "iceberg/kms/aws_sigv4_internal.h"
#include "iceberg/util/base64.h"
#include "iceberg/util/macros.h"

namespace iceberg::kms {

namespace {

std::string ToString(std::span<const uint8_t> bytes) {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

std::vector<uint8_t> ToBytes(std::string_view s) { return {s.begin(), s.end()}; }

std::string PropertyOr(const Properties& properties, std::string_view name,
                       std::string_view default_value) {
  auto it = properties.find(std::string(name));
  return it != properties.end() && !it->second.empty() ? it->second
                                                       : std::string(default_value);
}

std::string PropertyOrEnv(const Properties& properties, std::string_view name,
                          const char* env) {
  const char* value = std::getenv(env);
  return PropertyOr(properties, name, value != nullptr ? value : "");
}

/// POST a JSON request and parse the JSON response; non-2xx responses are errors.
Result<nlohmann::json> PostJson(HttpClient& http, HttpRequest request,
                                std::string_view service) {
  ICEBERG_ASSIGN_OR_RAISE(auto response, http.Post(request));
  if (response.status_code < 200 || response.status_code >= 300) {
    return IOError("{} request failed with HTTP {}: {}", service, response.status_code,
                   response.body);
  }
  auto json = nlohmann::json::parse(response.body, nullptr, /*allow_exceptions=*/false);
  if (json.is_discarded() || !json.is_object()) {
    return IOError("Invalid {} response: {}", service, response.body);
  }
  return json;
}

Result<std::string> GetString(const nlohmann::json& json, std::string_view field,
                              std::string_view service) {
  auto it = json.find(field);
  if (it == json.end() || !it->is_string()) {
    return IOError("Invalid {} response: missing string field {}", service, field);
  }
  return it->get<std::string>();
}

std::string AwsNow() {
  return std::format("{:%Y%m%dT%H%M%SZ}", std::chrono::floor<std::chrono::seconds>(
                                              std::chrono::system_clock::now()));
}

}  // namespace

// ---------------------------------------------------------------------------
// AWS KMS

struct AwsKmsClient::Impl {
  std::shared_ptr<HttpClient> http;
  Clock clock;
  std::string region;
  std::string endpoint;
  std::string encryption_algorithm;
  std::string data_key_spec;
  AwsCredentials credentials;

  Result<nlohmann::json> Call(std::string_view action, const nlohmann::json& body) {
    ICEBERG_PRECHECK(!endpoint.empty(), "AWS KMS client is not initialized");
    HttpRequest request{
        .url = endpoint,
        .headers = {{"Content-Type", "application/x-amz-json-1.1"},
                    {"X-Amz-Target", std::format("TrentService.{}", action)}},
        .body = body.dump()};
    ICEBERG_RETURN_UNEXPECTED(
        SignAwsRequest(request, "POST", region, "kms", credentials, clock()));
    return PostJson(*http, std::move(request), "AWS KMS");
  }
};

AwsKmsClient::AwsKmsClient(std::shared_ptr<HttpClient> http, Clock clock)
    : impl_(std::make_unique<Impl>()) {
  impl_->http = http != nullptr ? std::move(http) : HttpClient::MakeDefault();
  impl_->clock = clock ? std::move(clock) : Clock(AwsNow);
}

AwsKmsClient::~AwsKmsClient() = default;

Status AwsKmsClient::Initialize(const Properties& properties) {
  impl_->region = PropertyOrEnv(properties, kRegion, "AWS_REGION");
  ICEBERG_PRECHECK(!impl_->region.empty(), "AWS KMS client requires {} or AWS_REGION",
                   kRegion);
  impl_->endpoint = PropertyOr(
      properties, kEndpoint, std::format("https://kms.{}.amazonaws.com/", impl_->region));
  if (!impl_->endpoint.ends_with('/')) {
    impl_->endpoint += '/';
  }
  impl_->encryption_algorithm =
      PropertyOr(properties, kEncryptionAlgorithmSpec, "SYMMETRIC_DEFAULT");
  impl_->data_key_spec = PropertyOr(properties, kDataKeySpec, "AES_256");
  impl_->credentials = AwsCredentials{
      .access_key_id = PropertyOrEnv(properties, kAccessKeyId, "AWS_ACCESS_KEY_ID"),
      .secret_access_key =
          PropertyOrEnv(properties, kSecretAccessKey, "AWS_SECRET_ACCESS_KEY"),
      .session_token = PropertyOrEnv(properties, kSessionToken, "AWS_SESSION_TOKEN")};
  ICEBERG_PRECHECK(!impl_->credentials.access_key_id.empty() &&
                       !impl_->credentials.secret_access_key.empty(),
                   "AWS KMS client requires credentials ({}, {} or AWS_ACCESS_KEY_ID, "
                   "AWS_SECRET_ACCESS_KEY)",
                   kAccessKeyId, kSecretAccessKey);
  return {};
}

Result<std::vector<uint8_t>> AwsKmsClient::WrapKey(std::span<const uint8_t> key,
                                                   std::string_view wrapping_key_id) {
  ICEBERG_ASSIGN_OR_RAISE(
      auto response,
      impl_->Call("Encrypt", {{"KeyId", wrapping_key_id},
                              {"Plaintext", Base64::Encode(ToString(key))},
                              {"EncryptionAlgorithm", impl_->encryption_algorithm}}));
  ICEBERG_ASSIGN_OR_RAISE(auto blob, GetString(response, "CiphertextBlob", "AWS KMS"));
  ICEBERG_ASSIGN_OR_RAISE(auto wrapped, Base64::Decode(blob));
  return ToBytes(wrapped);
}

Result<std::vector<uint8_t>> AwsKmsClient::UnwrapKey(std::span<const uint8_t> wrapped_key,
                                                     std::string_view wrapping_key_id) {
  ICEBERG_ASSIGN_OR_RAISE(
      auto response,
      impl_->Call("Decrypt", {{"KeyId", wrapping_key_id},
                              {"CiphertextBlob", Base64::Encode(ToString(wrapped_key))},
                              {"EncryptionAlgorithm", impl_->encryption_algorithm}}));
  ICEBERG_ASSIGN_OR_RAISE(auto plaintext, GetString(response, "Plaintext", "AWS KMS"));
  ICEBERG_ASSIGN_OR_RAISE(auto key, Base64::Decode(plaintext));
  return ToBytes(key);
}

Result<KeyManagementClient::GeneratedKey> AwsKmsClient::GenerateKey(
    std::string_view wrapping_key_id) {
  ICEBERG_ASSIGN_OR_RAISE(
      auto response, impl_->Call("GenerateDataKey", {{"KeyId", wrapping_key_id},
                                                     {"KeySpec", impl_->data_key_spec}}));
  ICEBERG_ASSIGN_OR_RAISE(auto plaintext, GetString(response, "Plaintext", "AWS KMS"));
  ICEBERG_ASSIGN_OR_RAISE(auto blob, GetString(response, "CiphertextBlob", "AWS KMS"));
  ICEBERG_ASSIGN_OR_RAISE(auto key, Base64::Decode(plaintext));
  ICEBERG_ASSIGN_OR_RAISE(auto wrapped, Base64::Decode(blob));
  return GeneratedKey{.key = ToBytes(key), .wrapped_key = ToBytes(wrapped)};
}

// ---------------------------------------------------------------------------
// Google Cloud KMS

struct GcpKmsClient::Impl {
  std::shared_ptr<HttpClient> http;
  std::string endpoint;
  std::string token;

  Result<nlohmann::json> Call(std::string_view key_name, std::string_view method,
                              const nlohmann::json& body) {
    ICEBERG_PRECHECK(!token.empty(), "GCP KMS client is not initialized");
    HttpRequest request{.url = std::format("{}/v1/{}:{}", endpoint, key_name, method),
                        .headers = {{"Content-Type", "application/json"},
                                    {"Authorization", "Bearer " + token}},
                        .body = body.dump()};
    return PostJson(*http, std::move(request), "GCP KMS");
  }
};

GcpKmsClient::GcpKmsClient(std::shared_ptr<HttpClient> http)
    : impl_(std::make_unique<Impl>()) {
  impl_->http = http != nullptr ? std::move(http) : HttpClient::MakeDefault();
}

GcpKmsClient::~GcpKmsClient() = default;

Status GcpKmsClient::Initialize(const Properties& properties) {
  impl_->endpoint = PropertyOr(properties, kEndpoint, "https://cloudkms.googleapis.com");
  while (impl_->endpoint.ends_with('/')) {
    impl_->endpoint.pop_back();
  }
  impl_->token = PropertyOr(properties, kOAuth2Token, "");
  if (impl_->token.empty()) {
    return NotSupported(
        "GCP KMS client requires an OAuth2 token ({}); application default credentials "
        "are not supported",
        kOAuth2Token);
  }
  return {};
}

Result<std::vector<uint8_t>> GcpKmsClient::WrapKey(std::span<const uint8_t> key,
                                                   std::string_view wrapping_key_id) {
  ICEBERG_ASSIGN_OR_RAISE(auto response,
                          impl_->Call(wrapping_key_id, "encrypt",
                                      {{"plaintext", Base64::Encode(ToString(key))}}));
  ICEBERG_ASSIGN_OR_RAISE(auto ciphertext, GetString(response, "ciphertext", "GCP KMS"));
  ICEBERG_ASSIGN_OR_RAISE(auto wrapped, Base64::Decode(ciphertext));
  return ToBytes(wrapped);
}

Result<std::vector<uint8_t>> GcpKmsClient::UnwrapKey(std::span<const uint8_t> wrapped_key,
                                                     std::string_view wrapping_key_id) {
  ICEBERG_ASSIGN_OR_RAISE(
      auto response,
      impl_->Call(wrapping_key_id, "decrypt",
                  {{"ciphertext", Base64::Encode(ToString(wrapped_key))}}));
  ICEBERG_ASSIGN_OR_RAISE(auto plaintext, GetString(response, "plaintext", "GCP KMS"));
  ICEBERG_ASSIGN_OR_RAISE(auto key, Base64::Decode(plaintext));
  return ToBytes(key);
}

// ---------------------------------------------------------------------------
// Azure Key Vault

struct AzureKmsClient::Impl {
  static constexpr std::string_view kApiVersion = "7.4";

  std::shared_ptr<HttpClient> http;
  std::string vault_url;
  std::string algorithm;
  std::string token;

  Result<std::vector<uint8_t>> Call(std::string_view key_name, std::string_view method,
                                    std::span<const uint8_t> value) {
    ICEBERG_PRECHECK(!vault_url.empty(), "Azure KMS client is not initialized");
    nlohmann::json body{{"alg", algorithm},
                        {"value", Base64::UrlEncode(ToString(value))}};
    HttpRequest request{.url = std::format("{}/keys/{}/{}?api-version={}", vault_url,
                                           key_name, method, kApiVersion),
                        .headers = {{"Content-Type", "application/json"},
                                    {"Authorization", "Bearer " + token}},
                        .body = body.dump()};
    ICEBERG_ASSIGN_OR_RAISE(auto response,
                            PostJson(*http, std::move(request), "Azure Key Vault"));
    ICEBERG_ASSIGN_OR_RAISE(auto result, GetString(response, "value", "Azure Key Vault"));
    ICEBERG_ASSIGN_OR_RAISE(auto bytes, Base64::UrlDecode(result));
    return ToBytes(bytes);
  }
};

AzureKmsClient::AzureKmsClient(std::shared_ptr<HttpClient> http)
    : impl_(std::make_unique<Impl>()) {
  impl_->http = http != nullptr ? std::move(http) : HttpClient::MakeDefault();
}

AzureKmsClient::~AzureKmsClient() = default;

Status AzureKmsClient::Initialize(const Properties& properties) {
  impl_->vault_url = PropertyOr(properties, kVaultUrl, "");
  ICEBERG_PRECHECK(!impl_->vault_url.empty(), "Azure KMS client requires {}", kVaultUrl);
  while (impl_->vault_url.ends_with('/')) {
    impl_->vault_url.pop_back();
  }
  impl_->algorithm = PropertyOr(properties, kKeyWrapAlgorithm, "RSA-OAEP-256");
  impl_->token = PropertyOr(properties, kToken, "");
  ICEBERG_PRECHECK(!impl_->token.empty(),
                   "Azure KMS client requires an access token ({})", kToken);
  return {};
}

Result<std::vector<uint8_t>> AzureKmsClient::WrapKey(std::span<const uint8_t> key,
                                                     std::string_view wrapping_key_id) {
  return impl_->Call(wrapping_key_id, "wrapkey", key);
}

Result<std::vector<uint8_t>> AzureKmsClient::UnwrapKey(
    std::span<const uint8_t> wrapped_key, std::string_view wrapping_key_id) {
  return impl_->Call(wrapping_key_id, "unwrapkey", wrapped_key);
}

// ---------------------------------------------------------------------------
// Registration

void RegisterAll() {
  KmsRegistry::Register(std::string(KmsRegistry::kKmsTypeAws),
                        []() -> Result<std::unique_ptr<KeyManagementClient>> {
                          return std::make_unique<AwsKmsClient>();
                        });
  KmsRegistry::Register(std::string(KmsRegistry::kKmsTypeGcp),
                        []() -> Result<std::unique_ptr<KeyManagementClient>> {
                          return std::make_unique<GcpKmsClient>();
                        });
  KmsRegistry::Register(std::string(KmsRegistry::kKmsTypeAzure),
                        []() -> Result<std::unique_ptr<KeyManagementClient>> {
                          return std::make_unique<AzureKmsClient>();
                        });
}

namespace {

[[maybe_unused]] const bool kKmsClientsRegistered = []() {
  RegisterAll();
  return true;
}();

}  // namespace

}  // namespace iceberg::kms

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

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "iceberg/kms/aws_sigv4_internal.h"
#include "iceberg/test/matchers.h"
#include "iceberg/util/base64.h"

namespace iceberg::kms {

namespace {

using ::testing::HasSubstr;
using ::testing::StartsWith;

/// Records requests and answers with a fixed response.
class FakeHttpClient : public HttpClient {
 public:
  Result<HttpResponse> Post(const HttpRequest& request) override {
    requests.push_back(request);
    return response;
  }

  nlohmann::json LastBody() const { return nlohmann::json::parse(requests.back().body); }

  HttpResponse response{.status_code = 200, .body = "{}"};
  std::vector<HttpRequest> requests;
};

std::string ToString(const std::vector<uint8_t>& bytes) {
  return {bytes.begin(), bytes.end()};
}

const std::vector<uint8_t> kKey{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0xFB};
const std::vector<uint8_t> kWrapped{0xFF, 0xFE, 0x3E, 0x3F, 42};

}  // namespace

// AWS Signature Version 4 test suite, get-vanilla and post-vanilla.
TEST(AwsSigV4Test, TestSuiteVectors) {
  AwsCredentials credentials{
      .access_key_id = "AKIDEXAMPLE",
      .secret_access_key = "wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY"};
  for (const auto& [method, signature] : std::vector<std::pair<std::string, std::string>>{
           {"GET", "5fa00fa31553b73ebf1942676e86291e8372ff2a2260956d9b8aae1d763fbf31"},
           {"POST",
            "5da7c1a2acd57cee7505fc6676e4e544621c30862966e37dddb68e92efbe5d6b"}}) {
    HttpRequest request{.url = "https://example.amazonaws.com/"};
    ASSERT_THAT(SignAwsRequest(request, method, "us-east-1", "service", credentials,
                               "20150830T123600Z"),
                IsOk());
    EXPECT_EQ(request.headers.at("Authorization"),
              "AWS4-HMAC-SHA256 Credential=AKIDEXAMPLE/20150830/us-east-1/service/"
              "aws4_request, SignedHeaders=host;x-amz-date, Signature=" +
                  signature)
        << method;
  }
}

TEST(AwsSigV4Test, SessionTokenIsSigned) {
  AwsCredentials credentials{
      .access_key_id = "AKID", .secret_access_key = "SECRET", .session_token = "TOKEN"};
  HttpRequest request{.url = "https://kms.us-west-2.amazonaws.com/"};
  ASSERT_THAT(SignAwsRequest(request, "POST", "us-west-2", "kms", credentials,
                             "20240101T000000Z"),
              IsOk());
  EXPECT_EQ(request.headers.at("X-Amz-Security-Token"), "TOKEN");
  EXPECT_THAT(request.headers.at("Authorization"),
              HasSubstr("SignedHeaders=host;x-amz-date;x-amz-security-token,"));
}

TEST(AwsKmsClientTest, WrapUnwrapAndGenerate) {
  auto http = std::make_shared<FakeHttpClient>();
  AwsKmsClient client(http, [] { return std::string("20240102T030405Z"); });
  ASSERT_THAT(client.Initialize({{"client.region", "us-west-2"},
                                 {"kms.access-key-id", "AKID"},
                                 {"kms.secret-access-key", "SECRET"}}),
              IsOk());
  EXPECT_TRUE(client.SupportsKeyGeneration());

  http->response.body =
      nlohmann::json{{"CiphertextBlob", Base64::Encode(ToString(kWrapped))}}.dump();
  ICEBERG_UNWRAP_OR_FAIL(auto wrapped, client.WrapKey(kKey, "alias/table"));
  EXPECT_EQ(wrapped, kWrapped);
  const HttpRequest& request = http->requests.back();
  EXPECT_EQ(request.url, "https://kms.us-west-2.amazonaws.com/");
  EXPECT_EQ(request.headers.at("X-Amz-Target"), "TrentService.Encrypt");
  EXPECT_EQ(request.headers.at("Content-Type"), "application/x-amz-json-1.1");
  EXPECT_THAT(request.headers.at("Authorization"),
              StartsWith("AWS4-HMAC-SHA256 Credential=AKID/20240102/us-west-2/kms/"
                         "aws4_request, SignedHeaders=content-type;host;x-amz-date;"
                         "x-amz-target, Signature="));
  EXPECT_EQ(http->LastBody(),
            (nlohmann::json{{"KeyId", "alias/table"},
                            {"Plaintext", Base64::Encode(ToString(kKey))},
                            {"EncryptionAlgorithm", "SYMMETRIC_DEFAULT"}}));

  http->response.body =
      nlohmann::json{{"Plaintext", Base64::Encode(ToString(kKey))}}.dump();
  ICEBERG_UNWRAP_OR_FAIL(auto unwrapped, client.UnwrapKey(kWrapped, "alias/table"));
  EXPECT_EQ(unwrapped, kKey);
  EXPECT_EQ(http->requests.back().headers.at("X-Amz-Target"), "TrentService.Decrypt");
  EXPECT_EQ(http->LastBody()["CiphertextBlob"], Base64::Encode(ToString(kWrapped)));

  http->response.body =
      nlohmann::json{{"Plaintext", Base64::Encode(ToString(kKey))},
                     {"CiphertextBlob", Base64::Encode(ToString(kWrapped))}}
          .dump();
  ICEBERG_UNWRAP_OR_FAIL(auto generated, client.GenerateKey("alias/table"));
  EXPECT_EQ(generated.key, kKey);
  EXPECT_EQ(generated.wrapped_key, kWrapped);
  EXPECT_EQ(http->requests.back().headers.at("X-Amz-Target"),
            "TrentService.GenerateDataKey");
  EXPECT_EQ(http->LastBody(),
            (nlohmann::json{{"KeyId", "alias/table"}, {"KeySpec", "AES_256"}}));
}

TEST(AwsKmsClientTest, EndpointOverrideAndErrors) {
  auto http = std::make_shared<FakeHttpClient>();
  AwsKmsClient client(http);
  ASSERT_THAT(client.Initialize({{"client.region", "us-east-1"},
                                 {"kms.endpoint", "http://localhost:4566"},
                                 {"kms.access-key-id", "AKID"},
                                 {"kms.secret-access-key", "SECRET"}}),
              IsOk());

  http->response = {.status_code = 400, .body = R"({"__type":"NotFoundException"})"};
  auto result = client.WrapKey(kKey, "missing");
  EXPECT_THAT(result, IsError(ErrorKind::kIOError));
  EXPECT_THAT(result, HasErrorMessage("NotFoundException"));
  EXPECT_EQ(http->requests.back().url, "http://localhost:4566/");

  http->response = {.status_code = 200, .body = "{}"};
  EXPECT_THAT(client.UnwrapKey(kWrapped, "key"), HasErrorMessage("Plaintext"));
}

TEST(AwsKmsClientTest, RequiresRegionAndCredentials) {
  AwsKmsClient client(std::make_shared<FakeHttpClient>());
  if (std::getenv("AWS_REGION") == nullptr) {
    EXPECT_THAT(client.Initialize({}), IsError(ErrorKind::kInvalidArgument));
  }
  if (std::getenv("AWS_ACCESS_KEY_ID") == nullptr) {
    EXPECT_THAT(client.Initialize({{"client.region", "us-east-1"}}),
                IsError(ErrorKind::kInvalidArgument));
  }
}

TEST(GcpKmsClientTest, WrapAndUnwrap) {
  auto http = std::make_shared<FakeHttpClient>();
  GcpKmsClient client(http);
  ASSERT_THAT(client.Initialize({{"gcs.oauth2.token", "ya29.token"}}), IsOk());
  EXPECT_FALSE(client.SupportsKeyGeneration());
  const std::string key_name = "projects/p/locations/global/keyRings/r/cryptoKeys/k";

  http->response.body =
      nlohmann::json{{"ciphertext", Base64::Encode(ToString(kWrapped))}}.dump();
  ICEBERG_UNWRAP_OR_FAIL(auto wrapped, client.WrapKey(kKey, key_name));
  EXPECT_EQ(wrapped, kWrapped);
  EXPECT_EQ(http->requests.back().url,
            "https://cloudkms.googleapis.com/v1/" + key_name + ":encrypt");
  EXPECT_EQ(http->requests.back().headers.at("Authorization"), "Bearer ya29.token");
  EXPECT_EQ(http->LastBody(),
            (nlohmann::json{{"plaintext", Base64::Encode(ToString(kKey))}}));

  http->response.body =
      nlohmann::json{{"plaintext", Base64::Encode(ToString(kKey))}}.dump();
  ICEBERG_UNWRAP_OR_FAIL(auto unwrapped, client.UnwrapKey(kWrapped, key_name));
  EXPECT_EQ(unwrapped, kKey);
  EXPECT_EQ(http->requests.back().url,
            "https://cloudkms.googleapis.com/v1/" + key_name + ":decrypt");
}

TEST(GcpKmsClientTest, RequiresToken) {
  GcpKmsClient client(std::make_shared<FakeHttpClient>());
  EXPECT_THAT(client.Initialize({}), IsError(ErrorKind::kNotSupported));
}

TEST(AzureKmsClientTest, WrapAndUnwrap) {
  auto http = std::make_shared<FakeHttpClient>();
  AzureKmsClient client(http);
  ASSERT_THAT(client.Initialize({{"azure.keyvault.url", "https://vault.vault.azure.net/"},
                                 {"azure.keyvault.token", "entra-token"}}),
              IsOk());

  http->response.body = nlohmann::json{{"kid", "https://vault.vault.azure.net/keys/k/v1"},
                                       {"value", Base64::UrlEncode(ToString(kWrapped))}}
                            .dump();
  ICEBERG_UNWRAP_OR_FAIL(auto wrapped, client.WrapKey(kKey, "k/v1"));
  EXPECT_EQ(wrapped, kWrapped);
  EXPECT_EQ(http->requests.back().url,
            "https://vault.vault.azure.net/keys/k/v1/wrapkey?api-version=7.4");
  EXPECT_EQ(http->requests.back().headers.at("Authorization"), "Bearer entra-token");
  EXPECT_EQ(http->LastBody(),
            (nlohmann::json{{"alg", "RSA-OAEP-256"},
                            {"value", Base64::UrlEncode(ToString(kKey))}}));

  http->response.body =
      nlohmann::json{{"value", Base64::UrlEncode(ToString(kKey))}}.dump();
  ICEBERG_UNWRAP_OR_FAIL(auto unwrapped, client.UnwrapKey(kWrapped, "k/v1"));
  EXPECT_EQ(unwrapped, kKey);
  EXPECT_EQ(http->requests.back().url,
            "https://vault.vault.azure.net/keys/k/v1/unwrapkey?api-version=7.4");
}

TEST(AzureKmsClientTest, RequiresVaultUrlAndToken) {
  AzureKmsClient client(std::make_shared<FakeHttpClient>());
  EXPECT_THAT(client.Initialize({{"azure.keyvault.token", "t"}}),
              IsError(ErrorKind::kInvalidArgument));
  EXPECT_THAT(client.Initialize({{"azure.keyvault.url", "https://v.vault.azure.net"}}),
              IsError(ErrorKind::kInvalidArgument));
}

TEST(KmsClientsTest, RegisteredByType) {
  RegisterAll();
  ICEBERG_UNWRAP_OR_FAIL(auto client,
                         KmsRegistry::FromCatalogProperties(
                             {{"encryption.kms-type", "azure"},
                              {"azure.keyvault.url", "https://v.vault.azure.net"},
                              {"azure.keyvault.token", "t"}}));
  EXPECT_NE(dynamic_cast<AzureKmsClient*>(client.get()), nullptr);

  ICEBERG_UNWRAP_OR_FAIL(
      client, KmsRegistry::FromCatalogProperties(
                  {{"encryption.kms-type", "GCP"}, {"gcs.oauth2.token", "t"}}));
  EXPECT_NE(dynamic_cast<GcpKmsClient*>(client.get()), nullptr);

  ICEBERG_UNWRAP_OR_FAIL(
      client, KmsRegistry::FromCatalogProperties({{"encryption.kms-type", "aws"},
                                                  {"client.region", "eu-west-1"},
                                                  {"kms.access-key-id", "AKID"},
                                                  {"kms.secret-access-key", "SECRET"}}));
  EXPECT_NE(dynamic_cast<AwsKmsClient*>(client.get()), nullptr);
}

}  // namespace iceberg::kms

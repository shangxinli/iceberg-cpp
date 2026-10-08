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

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <vector>

#include <openssl/evp.h>
#include <openssl/hmac.h>

#include "iceberg/kms/aws_sigv4_internal.h"
#include "iceberg/util/macros.h"

namespace iceberg::kms {

namespace {

std::string ToHex(const unsigned char* data, size_t length) {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string out;
  out.reserve(length * 2);
  for (size_t i = 0; i < length; ++i) {
    out.push_back(kDigits[data[i] >> 4]);
    out.push_back(kDigits[data[i] & 0xF]);
  }
  return out;
}

Result<std::string> HmacSha256(std::string_view key, std::string_view data) {
  std::array<unsigned char, EVP_MAX_MD_SIZE> out{};
  unsigned int length = 0;
  if (HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
           reinterpret_cast<const unsigned char*>(data.data()), data.size(), out.data(),
           &length) == nullptr) {
    return IOError("HMAC-SHA256 failed");
  }
  return std::string(reinterpret_cast<const char*>(out.data()), length);
}

std::string ToLower(std::string_view s) {
  std::string out(s);
  std::ranges::transform(out, out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

std::string Trim(std::string_view s) {
  auto begin = s.find_first_not_of(" \t");
  if (begin == std::string_view::npos) {
    return "";
  }
  auto end = s.find_last_not_of(" \t");
  return std::string(s.substr(begin, end - begin + 1));
}

struct UrlParts {
  std::string host;
  std::string path;
  std::string query;
};

Result<UrlParts> ParseUrl(std::string_view url) {
  auto scheme_end = url.find("://");
  if (scheme_end == std::string_view::npos) {
    return InvalidArgument("Invalid URL: {}", url);
  }
  auto rest = url.substr(scheme_end + 3);
  auto path_start = rest.find('/');
  UrlParts parts;
  parts.host = std::string(rest.substr(0, path_start));
  auto path_and_query = path_start == std::string_view::npos ? std::string_view("/")
                                                             : rest.substr(path_start);
  auto query_start = path_and_query.find('?');
  parts.path = std::string(path_and_query.substr(0, query_start));
  if (query_start != std::string_view::npos) {
    parts.query = std::string(path_and_query.substr(query_start + 1));
  }
  return parts;
}

}  // namespace

Result<std::string> Sha256Hex(std::string_view data) {
  std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
  unsigned int length = 0;
  if (EVP_Digest(data.data(), data.size(), digest.data(), &length, EVP_sha256(),
                 nullptr) != 1) {
    return IOError("SHA-256 failed");
  }
  return ToHex(digest.data(), length);
}

Status SignAwsRequest(HttpRequest& request, std::string_view method,
                      std::string_view region, std::string_view service,
                      const AwsCredentials& credentials, std::string_view amz_date) {
  ICEBERG_PRECHECK(amz_date.size() == 16, "Invalid AWS date: {}", amz_date);
  ICEBERG_PRECHECK(
      !credentials.access_key_id.empty() && !credentials.secret_access_key.empty(),
      "AWS credentials are required to sign KMS requests");
  ICEBERG_ASSIGN_OR_RAISE(auto url, ParseUrl(request.url));

  request.headers["X-Amz-Date"] = std::string(amz_date);
  if (!credentials.session_token.empty()) {
    request.headers["X-Amz-Security-Token"] = credentials.session_token;
  }

  // Canonical headers: lower-case names, sorted, with Host
  std::map<std::string, std::string> canonical;
  canonical["host"] = url.host;
  for (const auto& [name, value] : request.headers) {
    canonical[ToLower(name)] = Trim(value);
  }
  std::string canonical_headers;
  std::string signed_headers;
  for (const auto& [name, value] : canonical) {
    canonical_headers += name + ":" + value + "\n";
    if (!signed_headers.empty()) {
      signed_headers += ";";
    }
    signed_headers += name;
  }

  ICEBERG_ASSIGN_OR_RAISE(auto payload_hash, Sha256Hex(request.body));
  // The KMS JSON API uses no query parameters; a query, if any, must already be in
  // canonical (sorted, encoded) form.
  std::string canonical_request =
      std::format("{}\n{}\n{}\n{}\n{}\n{}", method, url.path, url.query,
                  canonical_headers, signed_headers, payload_hash);

  std::string date(amz_date.substr(0, 8));
  std::string scope = std::format("{}/{}/{}/aws4_request", date, region, service);
  ICEBERG_ASSIGN_OR_RAISE(auto canonical_hash, Sha256Hex(canonical_request));
  std::string string_to_sign =
      std::format("AWS4-HMAC-SHA256\n{}\n{}\n{}", amz_date, scope, canonical_hash);

  ICEBERG_ASSIGN_OR_RAISE(auto k_date,
                          HmacSha256("AWS4" + credentials.secret_access_key, date));
  ICEBERG_ASSIGN_OR_RAISE(auto k_region, HmacSha256(k_date, region));
  ICEBERG_ASSIGN_OR_RAISE(auto k_service, HmacSha256(k_region, service));
  ICEBERG_ASSIGN_OR_RAISE(auto k_signing, HmacSha256(k_service, "aws4_request"));
  ICEBERG_ASSIGN_OR_RAISE(auto signature, HmacSha256(k_signing, string_to_sign));

  request.headers["Authorization"] = std::format(
      "AWS4-HMAC-SHA256 Credential={}/{}, SignedHeaders={}, Signature={}",
      credentials.access_key_id, scope, signed_headers,
      ToHex(reinterpret_cast<const unsigned char*>(signature.data()), signature.size()));
  return {};
}

}  // namespace iceberg::kms

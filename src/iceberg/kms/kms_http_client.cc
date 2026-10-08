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

#include "iceberg/kms/kms_http_client.h"

#include <cpr/cpr.h>

namespace iceberg::kms {

namespace {

class CprHttpClient : public HttpClient {
 public:
  Result<HttpResponse> Post(const HttpRequest& request) override {
    cpr::Header headers;
    for (const auto& [name, value] : request.headers) {
      headers.emplace(name, value);
    }
    auto response = cpr::Post(cpr::Url{request.url}, headers, cpr::Body{request.body},
                              cpr::Timeout{std::chrono::seconds(30)});
    if (response.error) {
      return IOError("KMS request to {} failed: {}", request.url, response.error.message);
    }
    return HttpResponse{.status_code = static_cast<int32_t>(response.status_code),
                        .body = std::move(response.text)};
  }
};

}  // namespace

std::shared_ptr<HttpClient> HttpClient::MakeDefault() {
  return std::make_shared<CprHttpClient>();
}

}  // namespace iceberg::kms

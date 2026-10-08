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

/// \file iceberg/kms/kms_http_client.h
/// Minimal HTTP transport used by the cloud KMS clients.

#include <map>
#include <memory>
#include <string>

#include "iceberg/kms/iceberg_kms_export.h"
#include "iceberg/result.h"

namespace iceberg::kms {

struct ICEBERG_KMS_EXPORT HttpRequest {
  std::string url;
  std::map<std::string, std::string> headers;
  std::string body;
};

struct ICEBERG_KMS_EXPORT HttpResponse {
  int32_t status_code = 0;
  std::string body;
};

/// \brief Sends KMS requests. Replaceable in tests.
class ICEBERG_KMS_EXPORT HttpClient {
 public:
  virtual ~HttpClient() = default;

  /// \brief POST the request and return the response, whatever its status code.
  virtual Result<HttpResponse> Post(const HttpRequest& request) = 0;

  /// \brief The default client (cpr / libcurl).
  static std::shared_ptr<HttpClient> MakeDefault();
};

}  // namespace iceberg::kms

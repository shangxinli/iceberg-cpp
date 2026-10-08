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

/// \file iceberg/kms/aws_sigv4_internal.h
/// AWS Signature Version 4 signing of HTTP requests.

#include <map>
#include <string>
#include <string_view>

#include "iceberg/kms/kms_http_client.h"
#include "iceberg/result.h"

namespace iceberg::kms {

struct AwsCredentials {
  std::string access_key_id;
  std::string secret_access_key;
  std::string session_token;
};

/// \brief Lower-case hex SHA-256 of `data`.
Result<std::string> Sha256Hex(std::string_view data);

/// \brief Sign `request` (method `method`) for AWS SigV4.
///
/// Adds the X-Amz-Date, X-Amz-Security-Token (if any) and Authorization headers. All
/// headers of the request, and Host (from the URL), are signed.
///
/// \param amz_date the request time, as yyyyMMdd'T'HHmmss'Z'
Status SignAwsRequest(HttpRequest& request, std::string_view method,
                      std::string_view region, std::string_view service,
                      const AwsCredentials& credentials, std::string_view amz_date);

}  // namespace iceberg::kms

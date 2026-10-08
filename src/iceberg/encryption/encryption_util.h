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

/// \file iceberg/encryption/encryption_util.h
/// Table-level encryption policy.

#include <memory>

#include "iceberg/iceberg_export.h"
#include "iceberg/result.h"
#include "iceberg/type_fwd.h"

namespace iceberg {

class FileIO;

/// \brief Table-level encryption helpers.
struct ICEBERG_EXPORT EncryptionUtil {
  /// \brief Whether the table is encrypted (has the `encryption.key-id` property).
  static bool IsEncrypted(const TableMetadata& metadata);

  /// \brief Fails while writing to encrypted tables is not supported.
  ///
  /// TODO: remove once encrypted manifests, manifest lists and data files are written.
  static Status CheckWriteSupported(const TableMetadata& metadata);

  /// \brief Fails if `io` is the FileIO of an encrypted table and writing encrypted
  /// files is not supported yet.
  static Status CheckWriteSupported(const std::shared_ptr<FileIO>& io);
};

}  // namespace iceberg

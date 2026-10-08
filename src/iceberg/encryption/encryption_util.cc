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

#include "iceberg/encryption/encryption_util.h"

#include "iceberg/encryption/encrypting_file_io.h"
#include "iceberg/table_metadata.h"
#include "iceberg/table_properties.h"

namespace iceberg {

bool EncryptionUtil::IsEncrypted(const TableMetadata& metadata) {
  return !metadata.properties.Get(TableProperties::kEncryptionTableKey).empty();
}

Status EncryptionUtil::CheckWriteSupported(const TableMetadata& metadata) {
  if (IsEncrypted(metadata)) {
    return NotSupported("Writing to encrypted tables is not supported yet");
  }
  return {};
}

Status EncryptionUtil::CheckWriteSupported(const std::shared_ptr<FileIO>& io) {
  if (EncryptingFileIO::From(io) != nullptr) {
    return NotSupported("Writing to encrypted tables is not supported yet");
  }
  return {};
}

}  // namespace iceberg

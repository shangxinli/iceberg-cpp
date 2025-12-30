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

/// \file iceberg/data/position_delete_writer.h
/// Writer for Iceberg position delete files.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "iceberg/arrow_c_data.h"
#include "iceberg/data/writer.h"
#include "iceberg/file_format.h"
#include "iceberg/iceberg_export.h"
#include "iceberg/result.h"
#include "iceberg/row/partition_values.h"
#include "iceberg/type_fwd.h"

namespace iceberg {

/// \brief Options for creating a PositionDeleteWriter.
///
/// \note The following features from Java PositionDeleteWriter are not yet supported:
/// - Encryption key metadata
/// - Referenced data files tracking (CharSequenceSet referencedDataFiles)
/// - Metrics stripping for multi-file deletes
/// - Split offsets tracking
struct ICEBERG_EXPORT PositionDeleteWriterOptions {
  std::string path;
  std::shared_ptr<Schema> schema;
  std::shared_ptr<PartitionSpec> spec;
  PartitionValues partition;
  FileFormatType format = FileFormatType::kParquet;
  std::shared_ptr<FileIO> io;
  std::shared_ptr<Schema> row_schema;  // Optional row data schema
  std::shared_ptr<class WriterProperties> properties;
};

/// \brief Writer for Iceberg position delete files.
///
/// Position delete files contain records identifying rows to delete by their
/// file path and position within the file. Each record must contain:
/// - file_path (string): the data file containing the deleted row
/// - pos (long): the row position in the data file (0-indexed)
/// - Optional row data: additional columns from the deleted row
///
/// Position deletes are required to be sorted by (file_path, pos).
///
/// \note Thread Safety: This class is NOT thread-safe. External synchronization
/// is required if multiple threads need to write concurrently.
class ICEBERG_EXPORT PositionDeleteWriter : public FileWriter {
 public:
  /// \brief Create a position delete writer.
  ///
  /// \param options Configuration options for the writer.
  /// \return A new PositionDeleteWriter instance or an error.
  static Result<std::unique_ptr<PositionDeleteWriter>> Make(
      const PositionDeleteWriterOptions& options);

  ~PositionDeleteWriter() override;

  /// \brief Write a batch of position delete records.
  ///
  /// The input array must conform to the position delete schema with at least
  /// file_path and pos columns. Records should be sorted by (file_path, pos).
  ///
  /// \param data Arrow array containing delete records.
  /// \return Status indicating success or failure.
  Status Write(ArrowArray* data) override;

  /// \brief Write a single position delete record.
  ///
  /// Convenience method for writing individual deletes without creating an Arrow array.
  /// For bulk deletes, use Write(ArrowArray*) for better performance.
  ///
  /// \param file_path Path to the data file containing the row to delete.
  /// \param pos Position (0-indexed) of the row in the data file.
  /// \return Status indicating success or failure.
  Status WriteDelete(std::string_view file_path, int64_t pos);

  /// \brief Get the current number of bytes written.
  ///
  /// \return Result containing the number of bytes written or an error.
  Result<int64_t> Length() const override;

  /// \brief Close the writer and finalize the file.
  ///
  /// This method is idempotent - calling it multiple times is safe.
  ///
  /// \return Status indicating success or failure.
  Status Close() override;

  /// \brief Get file metadata for all files produced by this writer.
  ///
  /// This method should be called after Close(). Position delete writers may
  /// produce multiple file-scoped delete files.
  ///
  /// \return Result containing the write result or an error.
  Result<WriteResult> Metadata() override;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
  explicit PositionDeleteWriter(std::unique_ptr<Impl> impl);
};

}  // namespace iceberg

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

#include "iceberg/data/position_delete_writer.h"

#include <vector>

#include "iceberg/manifest/manifest_entry.h"

namespace iceberg {

//=============================================================================
// PositionDeleteWriter::Impl
//=============================================================================

class PositionDeleteWriter::Impl {
 public:
  explicit Impl(const PositionDeleteWriterOptions& options)
      : options_(options), bytes_written_(0), is_closed_(false) {}

  PositionDeleteWriterOptions options_;
  int64_t bytes_written_;
  bool is_closed_;
  std::vector<std::shared_ptr<DataFile>> completed_files_;

  // TODO: Add the following when implementing:
  // - FileAppender for position delete format (file_path + position columns)
  // - CharSequenceSet or equivalent for tracking referenced data files
  // - Metrics collection with field stripping for multi-file deletes
  // - Split offsets tracking
  // - Encryption key metadata handling
  // - Sorting enforcement/verification for (file_path, pos) ordering
};

//=============================================================================
// PositionDeleteWriter
//=============================================================================

Result<std::unique_ptr<PositionDeleteWriter>> PositionDeleteWriter::Make(
    const PositionDeleteWriterOptions& options) {
  // Validate required fields
  if (!options.schema) {
    return InvalidArgument("PositionDeleteWriter: schema is required");
  }
  if (!options.spec) {
    return InvalidArgument("PositionDeleteWriter: partition spec is required");
  }
  if (!options.io) {
    return InvalidArgument("PositionDeleteWriter: FileIO is required");
  }
  if (options.path.empty()) {
    return InvalidArgument("PositionDeleteWriter: path cannot be empty");
  }

  // Position delete schema must have at least file_path and pos columns
  // TODO: Validate schema structure:
  // - Field 2147483546 (file_path): required string
  // - Field 2147483545 (pos): required long
  // - Optional row data fields from row_schema

  auto impl = std::make_unique<Impl>(options);
  return std::unique_ptr<PositionDeleteWriter>(
      new PositionDeleteWriter(std::move(impl)));
}

PositionDeleteWriter::PositionDeleteWriter(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

PositionDeleteWriter::~PositionDeleteWriter() = default;

Status PositionDeleteWriter::Write(ArrowArray* data) {
  if (impl_->is_closed_) {
    return Invalid("PositionDeleteWriter: cannot write after Close()");
  }
  if (!data) {
    return InvalidArgument("PositionDeleteWriter::Write: data cannot be null");
  }

  // TODO: Implement batch write using FileAppender
  // - Validate data schema matches position delete schema (file_path, pos, optional row data)
  // - Verify records are sorted by (file_path, pos)
  // - Write batch to file using format-specific appender (Parquet/Avro/ORC)
  // - Update bytes_written_
  // - Track referenced data files in CharSequenceSet equivalent
  // - Collect metrics (record count, column sizes, bounds, etc.)
  // - Handle file rolling if multiple output files are needed
  return NotImplemented("PositionDeleteWriter::Write not yet implemented");
}

Status PositionDeleteWriter::WriteDelete(std::string_view file_path, int64_t pos) {
  if (impl_->is_closed_) {
    return Invalid("PositionDeleteWriter: cannot write after Close()");
  }
  if (file_path.empty()) {
    return InvalidArgument("PositionDeleteWriter::WriteDelete: file_path cannot be empty");
  }
  if (pos < 0) {
    return InvalidArgument(
        "PositionDeleteWriter::WriteDelete: position must be non-negative, got {}", pos);
  }

  // TODO: Implement single delete write
  // - Create Arrow record with file_path (string) and pos (long) fields
  // - Add row data fields if row_schema is provided
  // - Write to file using FileAppender
  // - Update bytes_written_
  // - Track file_path in referenced data files set
  // - Ensure proper sorting with previous writes
  return NotImplemented("PositionDeleteWriter::WriteDelete not yet implemented");
}

Result<int64_t> PositionDeleteWriter::Length() const {
  // Return current bytes written even if not closed
  return impl_->bytes_written_;
}

Status PositionDeleteWriter::Close() {
  if (impl_->is_closed_) {
    return {};  // Idempotent close
  }

  // TODO: Implement file finalization
  // - Flush FileAppender and close underlying file
  // - Collect final metrics for all written records
  // - Create DataFile metadata with:
  //   * content = Content::kPositionDeletes
  //   * file_path, file_format, partition
  //   * record_count, file_size_in_bytes
  //   * column_sizes, value_counts, null_value_counts, nan_value_counts
  //   * lower_bounds, upper_bounds
  //   * sort_order_id = null (position deletes should not have sort order)
  // - Add completed file(s) to completed_files_
  // - Handle metrics stripping for multi-file deletes

  impl_->is_closed_ = true;
  return NotImplemented("PositionDeleteWriter::Close not yet implemented");
}

Result<FileWriter::WriteResult> PositionDeleteWriter::Metadata() {
  if (!impl_->is_closed_) {
    return Invalid("PositionDeleteWriter::Metadata: can only be called after Close()");
  }

  // TODO: Return completed files with metrics
  // WriteResult result;
  // result.data_files = impl_->completed_files_;
  // return result;

  return NotImplemented("PositionDeleteWriter::Metadata not yet implemented");
}

}  // namespace iceberg

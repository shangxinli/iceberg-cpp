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

#include "iceberg/data/writer.h"

namespace iceberg {

//=============================================================================
// DataWriter::Impl
//=============================================================================

class DataWriter::Impl {
 public:
  explicit Impl(const DataWriterOptions& options) : options_(options) {}

  DataWriterOptions options_;

  // TODO: Add the following when implementing:
  // - FileAppender or equivalent writer for the format
  // - Metrics collection
  // - Split offsets tracking
  // - Encryption key metadata handling
};

//=============================================================================
// DataWriter
//=============================================================================

Result<std::unique_ptr<DataWriter>> DataWriter::Make(const DataWriterOptions& options) {
  // Validate required fields
  if (!options.schema) {
    return InvalidArgument("DataWriter: schema is required");
  }
  if (!options.spec) {
    return InvalidArgument("DataWriter: partition spec is required");
  }
  if (!options.io) {
    return InvalidArgument("DataWriter: FileIO is required");
  }
  if (options.path.empty()) {
    return InvalidArgument("DataWriter: path cannot be empty");
  }
  return NotImplemented("DataWriter implementation not yet available");
}

DataWriter::DataWriter(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

DataWriter::~DataWriter() = default;

Status DataWriter::Write(ArrowArray* data) {
  return NotImplemented("DataWriter::Write not yet implemented");
}

Result<int64_t> DataWriter::Length() const {
  return NotImplemented("DataWriter::Length not yet implemented");
}

Status DataWriter::Close() {
  return NotImplemented("DataWriter::Close not yet implemented");
}

Result<FileWriter::WriteResult> DataWriter::Metadata() {
  return NotImplemented("DataWriter::Metadata not yet implemented");
}

//=============================================================================
// PositionDeleteWriter::Impl
//=============================================================================

class PositionDeleteWriter::Impl {
 public:
  explicit Impl(const PositionDeleteWriterOptions& options) : options_(options) {}

  PositionDeleteWriterOptions options_;

  // TODO: Add the following when implementing:
  // - FileAppender for position delete format (file path + position columns)
  // - CharSequenceSet or equivalent for tracking referenced data files
  // - Metrics collection with field stripping for multi-file deletes
  // - Split offsets tracking
  // - Encryption key metadata handling
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
  return NotImplemented("PositionDeleteWriter implementation not yet available");
}

PositionDeleteWriter::PositionDeleteWriter(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

PositionDeleteWriter::~PositionDeleteWriter() = default;

Status PositionDeleteWriter::Write(ArrowArray* data) {
  return NotImplemented("PositionDeleteWriter::Write not yet implemented");
}

Status PositionDeleteWriter::WriteDelete(std::string_view file_path, int64_t pos) {
  return NotImplemented("PositionDeleteWriter::WriteDelete not yet implemented");
}

Result<int64_t> PositionDeleteWriter::Length() const {
  return NotImplemented("PositionDeleteWriter::Length not yet implemented");
}

Status PositionDeleteWriter::Close() {
  return NotImplemented("PositionDeleteWriter::Close not yet implemented");
}

Result<FileWriter::WriteResult> PositionDeleteWriter::Metadata() {
  return NotImplemented("PositionDeleteWriter::Metadata not yet implemented");
}

//=============================================================================
// EqualityDeleteWriter::Impl
//=============================================================================

class EqualityDeleteWriter::Impl {
 public:
  explicit Impl(const EqualityDeleteWriterOptions& options) : options_(options) {}

  EqualityDeleteWriterOptions options_;

  // TODO: Add the following when implementing:
  // - FileAppender for equality delete format
  // - Metrics collection
  // - Split offsets tracking
  // - Encryption key metadata handling
};

//=============================================================================
// EqualityDeleteWriter
//=============================================================================

Result<std::unique_ptr<EqualityDeleteWriter>> EqualityDeleteWriter::Make(
    const EqualityDeleteWriterOptions& options) {
  // Validate required fields
  if (!options.schema) {
    return InvalidArgument("EqualityDeleteWriter: schema is required");
  }
  if (!options.spec) {
    return InvalidArgument("EqualityDeleteWriter: partition spec is required");
  }
  if (!options.io) {
    return InvalidArgument("EqualityDeleteWriter: FileIO is required");
  }
  if (options.path.empty()) {
    return InvalidArgument("EqualityDeleteWriter: path cannot be empty");
  }
  if (options.equality_field_ids.empty()) {
    return InvalidArgument("EqualityDeleteWriter: equality_field_ids cannot be empty");
  }
  return NotImplemented("EqualityDeleteWriter implementation not yet available");
}

EqualityDeleteWriter::EqualityDeleteWriter(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

EqualityDeleteWriter::~EqualityDeleteWriter() = default;

Status EqualityDeleteWriter::Write(ArrowArray* data) {
  return NotImplemented("EqualityDeleteWriter::Write not yet implemented");
}

Result<int64_t> EqualityDeleteWriter::Length() const {
  return NotImplemented("EqualityDeleteWriter::Length not yet implemented");
}

Status EqualityDeleteWriter::Close() {
  return NotImplemented("EqualityDeleteWriter::Close not yet implemented");
}

Result<FileWriter::WriteResult> EqualityDeleteWriter::Metadata() {
  return NotImplemented("EqualityDeleteWriter::Metadata not yet implemented");
}

const std::vector<int32_t>& EqualityDeleteWriter::equality_field_ids() const {
  return impl_->options_.equality_field_ids;
}

//=============================================================================
// FileWriterFactory::Impl
//=============================================================================

class FileWriterFactory::Impl {
 public:
  Impl(std::shared_ptr<Schema> schema, std::shared_ptr<PartitionSpec> spec,
       std::shared_ptr<FileIO> io, std::shared_ptr<WriterProperties> properties)
      : schema_(std::move(schema)),
        spec_(std::move(spec)),
        io_(std::move(io)),
        properties_(std::move(properties)) {}

  std::shared_ptr<Schema> schema_;
  std::shared_ptr<PartitionSpec> spec_;
  std::shared_ptr<FileIO> io_;
  std::shared_ptr<WriterProperties> properties_;

  std::shared_ptr<Schema> eq_delete_schema_;
  std::vector<int32_t> equality_field_ids_;
  std::shared_ptr<Schema> pos_delete_row_schema_;
};

//=============================================================================
// FileWriterFactory
//=============================================================================

FileWriterFactory::FileWriterFactory(std::shared_ptr<Schema> schema,
                                     std::shared_ptr<PartitionSpec> spec,
                                     std::shared_ptr<FileIO> io,
                                     std::shared_ptr<WriterProperties> properties)
    : impl_(std::make_unique<Impl>(std::move(schema), std::move(spec), std::move(io),
                                    std::move(properties))) {}

FileWriterFactory::~FileWriterFactory() = default;

void FileWriterFactory::SetEqualityDeleteConfig(std::shared_ptr<Schema> eq_delete_schema,
                                                 std::vector<int32_t> equality_field_ids) {
  impl_->eq_delete_schema_ = std::move(eq_delete_schema);
  impl_->equality_field_ids_ = std::move(equality_field_ids);
}

void FileWriterFactory::SetPositionDeleteRowSchema(
    std::shared_ptr<Schema> pos_delete_row_schema) {
  impl_->pos_delete_row_schema_ = std::move(pos_delete_row_schema);
}

Result<std::unique_ptr<DataWriter>> FileWriterFactory::NewDataWriter(
    std::string path, FileFormatType format, PartitionValues partition,
    std::optional<int32_t> sort_order_id) {
  DataWriterOptions options;
  options.path = std::move(path);
  options.schema = impl_->schema_;
  options.spec = impl_->spec_;
  options.partition = std::move(partition);
  options.format = format;
  options.io = impl_->io_;
  options.sort_order_id = sort_order_id;
  options.properties = impl_->properties_;

  return DataWriter::Make(options);
}

Result<std::unique_ptr<PositionDeleteWriter>> FileWriterFactory::NewPositionDeleteWriter(
    std::string path, FileFormatType format, PartitionValues partition) {
  PositionDeleteWriterOptions options;
  options.path = std::move(path);
  options.schema = impl_->schema_;
  options.spec = impl_->spec_;
  options.partition = std::move(partition);
  options.format = format;
  options.io = impl_->io_;
  options.row_schema = impl_->pos_delete_row_schema_;
  options.properties = impl_->properties_;

  return PositionDeleteWriter::Make(options);
}

Result<std::unique_ptr<EqualityDeleteWriter>> FileWriterFactory::NewEqualityDeleteWriter(
    std::string path, FileFormatType format, PartitionValues partition,
    std::optional<int32_t> sort_order_id) {
  EqualityDeleteWriterOptions options;
  options.path = std::move(path);
  options.schema = impl_->eq_delete_schema_ ? impl_->eq_delete_schema_ : impl_->schema_;
  options.spec = impl_->spec_;
  options.partition = std::move(partition);
  options.format = format;
  options.io = impl_->io_;
  // Note: equality_field_ids is copied here. This is acceptable because:
  // 1. The vector is typically small (a few field IDs)
  // 2. Java implementation follows the same pattern (stores and reuses)
  // 3. The factory is expected to be created once and reused for many writers
  options.equality_field_ids = impl_->equality_field_ids_;
  options.sort_order_id = sort_order_id;
  options.properties = impl_->properties_;

  return EqualityDeleteWriter::Make(options);
}

}  // namespace iceberg

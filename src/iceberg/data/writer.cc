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

#include "iceberg/file_writer.h"
#include "iceberg/manifest/manifest_entry.h"
#include "iceberg/util/conversions.h"

namespace iceberg {

FileWriter::~FileWriter() = default;

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
  std::unique_ptr<Writer> format_writer_;
  bool closed_ = false;
};

//=============================================================================
// EqualityDeleteWriter
//=============================================================================

Result<std::unique_ptr<EqualityDeleteWriter>> EqualityDeleteWriter::Make(
    const EqualityDeleteWriterOptions& options) {
  // Validate required fields
  if (options.path.empty()) {
    return InvalidArgument("EqualityDeleteWriter path cannot be empty");
  }
  if (!options.schema) {
    return InvalidArgument("EqualityDeleteWriter schema cannot be null");
  }
  if (!options.io) {
    return InvalidArgument("EqualityDeleteWriter io cannot be null");
  }
  if (options.equality_field_ids.empty()) {
    return InvalidArgument("EqualityDeleteWriter equality_field_ids cannot be empty");
  }

  auto impl = std::make_unique<Impl>(options);

  // Create WriterOptions for the format-specific writer
  WriterOptions writer_options;
  writer_options.path = options.path;
  writer_options.schema = options.schema;
  writer_options.io = options.io;
  writer_options.properties = options.properties;

  // Create format-specific writer via registry
  ICEBERG_ASSIGN_OR_RAISE(impl->format_writer_,
                          WriterFactoryRegistry::Open(options.format, writer_options));

  return std::unique_ptr<EqualityDeleteWriter>(new EqualityDeleteWriter(std::move(impl)));
}

EqualityDeleteWriter::EqualityDeleteWriter(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

EqualityDeleteWriter::~EqualityDeleteWriter() = default;

Status EqualityDeleteWriter::Write(ArrowArray* data) {
  if (impl_->closed_) {
    return Invalid("Cannot write to a closed EqualityDeleteWriter");
  }
  if (!data) {
    return InvalidArgument("Cannot write null data to EqualityDeleteWriter");
  }
  // Delegate to format writer
  return impl_->format_writer_->Write(data);
}

Result<int64_t> EqualityDeleteWriter::Length() const {
  return impl_->format_writer_->length();
}

Status EqualityDeleteWriter::Close() {
  if (!impl_->closed_) {
    ICEBERG_RETURN_UNEXPECTED(impl_->format_writer_->Close());
    impl_->closed_ = true;
  }
  return {};
}

Result<FileWriter::WriteResult> EqualityDeleteWriter::Metadata() {
  if (!impl_->closed_) {
    return Invalid("Writer must be closed before getting metadata");
  }

  WriteResult result;
  auto data_file = std::make_shared<DataFile>();

  // Set equality delete specific fields
  data_file->content = DataFile::Content::kEqualityDeletes;
  data_file->file_path = impl_->options_.path;
  data_file->file_format = impl_->options_.format;
  data_file->partition = impl_->options_.partition;
  data_file->equality_ids = impl_->options_.equality_field_ids;
  data_file->sort_order_id = impl_->options_.sort_order_id;

  // Get metrics from format writer
  ICEBERG_ASSIGN_OR_RAISE(auto metrics, impl_->format_writer_->metrics());
  if (metrics.row_count.has_value()) {
    data_file->record_count = *metrics.row_count;
  }

  // Get file size
  ICEBERG_ASSIGN_OR_RAISE(auto length, impl_->format_writer_->length());
  data_file->file_size_in_bytes = length;

  // Get split offsets
  data_file->split_offsets = impl_->format_writer_->split_offsets();

  // Copy metrics maps (convert from unordered_map to map)
  for (const auto& [field_id, size] : metrics.column_sizes) {
    data_file->column_sizes[field_id] = size;
  }
  for (const auto& [field_id, count] : metrics.value_counts) {
    data_file->value_counts[field_id] = count;
  }
  for (const auto& [field_id, count] : metrics.null_value_counts) {
    data_file->null_value_counts[field_id] = count;
  }
  for (const auto& [field_id, count] : metrics.nan_value_counts) {
    data_file->nan_value_counts[field_id] = count;
  }

  // Convert Literal bounds to binary format
  for (const auto& [field_id, literal] : metrics.lower_bounds) {
    ICEBERG_ASSIGN_OR_RAISE(auto bytes, Conversions::ToBytes(literal));
    data_file->lower_bounds[field_id] = std::move(bytes);
  }

  for (const auto& [field_id, literal] : metrics.upper_bounds) {
    ICEBERG_ASSIGN_OR_RAISE(auto bytes, Conversions::ToBytes(literal));
    data_file->upper_bounds[field_id] = std::move(bytes);
  }

  result.data_files.push_back(data_file);
  return result;
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
    const std::string& path, FileFormatType format, const PartitionValues& partition,
    std::optional<int32_t> sort_order_id) {
  DataWriterOptions options;
  options.path = path;
  options.schema = impl_->schema_;
  options.spec = impl_->spec_;
  options.partition = partition;
  options.format = format;
  options.io = impl_->io_;
  options.sort_order_id = sort_order_id;
  options.properties = impl_->properties_;

  return DataWriter::Make(options);
}

Result<std::unique_ptr<PositionDeleteWriter>> FileWriterFactory::NewPositionDeleteWriter(
    const std::string& path, FileFormatType format, const PartitionValues& partition) {
  PositionDeleteWriterOptions options;
  options.path = path;
  options.schema = impl_->schema_;
  options.spec = impl_->spec_;
  options.partition = partition;
  options.format = format;
  options.io = impl_->io_;
  options.row_schema = impl_->pos_delete_row_schema_;
  options.properties = impl_->properties_;

  return PositionDeleteWriter::Make(options);
}

Result<std::unique_ptr<EqualityDeleteWriter>> FileWriterFactory::NewEqualityDeleteWriter(
    const std::string& path, FileFormatType format, const PartitionValues& partition,
    std::optional<int32_t> sort_order_id) {
  EqualityDeleteWriterOptions options;
  options.path = path;
  options.schema = impl_->eq_delete_schema_ ? impl_->eq_delete_schema_ : impl_->schema_;
  options.spec = impl_->spec_;
  options.partition = partition;
  options.format = format;
  options.io = impl_->io_;
  options.equality_field_ids = impl_->equality_field_ids_;
  options.sort_order_id = sort_order_id;
  options.properties = impl_->properties_;

  return EqualityDeleteWriter::Make(options);
}

}  // namespace iceberg

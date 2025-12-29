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

/// \file iceberg/data/writer.h
/// Base interface for Iceberg data file writers.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "iceberg/arrow_c_data.h"
#include "iceberg/file_format.h"
#include "iceberg/iceberg_export.h"
#include "iceberg/manifest/manifest_entry.h"
#include "iceberg/result.h"
#include "iceberg/type_fwd.h"

namespace iceberg {

/// \brief Base interface for data file writers.
///
/// This interface defines the common operations for writing Iceberg data files,
/// including data files, equality delete files, and position delete files.
///
/// Typical usage:
/// 1. Create a writer instance (via concrete implementation)
/// 2. Call Write() one or more times to write data
/// 3. Call Close() to finalize the file
/// 4. Call Metadata() to get file metadata (only valid after Close())
///
/// \note This interface is not thread-safe. Concurrent calls to Write()
/// from multiple threads on the same instance are not supported.
class ICEBERG_EXPORT FileWriter {
 public:
  virtual ~FileWriter() = default;

  /// \brief Write a batch of records.
  ///
  /// \param data Arrow array containing the records to write.
  /// \return Status indicating success or failure.
  virtual Status Write(ArrowArray* data) = 0;

  /// \brief Get the current number of bytes written.
  ///
  /// \return Result containing the number of bytes written or an error.
  virtual Result<int64_t> Length() const = 0;

  /// \brief Close the writer and finalize the file.
  ///
  /// \return Status indicating success or failure.
  virtual Status Close() = 0;

  /// \brief File metadata for all files produced by the writer.
  ///
  /// \note The following features from Java are not yet supported:
  /// - Encryption key metadata (EncryptionKeyMetadata)
  /// - Split offsets for data files
  /// - Referenced data files tracking for position deletes
  struct ICEBERG_EXPORT WriteResult {
    /// Usually a writer produces a single data or delete file.
    /// Position delete writer may produce multiple file-scoped delete files.
    /// In the future, multiple files can be produced if file rolling is supported.
    std::vector<std::shared_ptr<DataFile>> data_files;
  };

  /// \brief Get file metadata for all files produced by this writer.
  ///
  /// This method should be called after Close() to retrieve the metadata
  /// for all files written by this writer.
  ///
  /// \return Result containing the write result or an error.
  virtual Result<WriteResult> Metadata() = 0;
};

//=============================================================================
// DataWriter
//=============================================================================

/// \brief Options for creating a DataWriter.
///
/// \note The following features from Java DataWriter are not yet supported:
/// - Encryption key metadata (uses FileIO instead of EncryptedOutputFile)
/// - Metrics collection and reporting
/// - Split offsets tracking
struct ICEBERG_EXPORT DataWriterOptions {
  std::string path;
  std::shared_ptr<Schema> schema;
  std::shared_ptr<PartitionSpec> spec;
  PartitionValues partition;
  FileFormatType format = FileFormatType::kParquet;
  std::shared_ptr<FileIO> io;
  std::optional<int32_t> sort_order_id;
  std::shared_ptr<class WriterProperties> properties;
};

/// \brief Writer for Iceberg data files.
class ICEBERG_EXPORT DataWriter : public FileWriter {
 public:
  static Result<std::unique_ptr<DataWriter>> Make(const DataWriterOptions& options);
  ~DataWriter() override;

  Status Write(ArrowArray* data) override;
  Result<int64_t> Length() const override;
  Status Close() override;
  Result<WriteResult> Metadata() override;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
  explicit DataWriter(std::unique_ptr<Impl> impl);
};

//=============================================================================
// PositionDeleteWriter
//=============================================================================

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
class ICEBERG_EXPORT PositionDeleteWriter : public FileWriter {
 public:
  static Result<std::unique_ptr<PositionDeleteWriter>> Make(
      const PositionDeleteWriterOptions& options);
  ~PositionDeleteWriter() override;

  Status Write(ArrowArray* data) override;
  Status WriteDelete(std::string_view file_path, int64_t pos);
  Result<int64_t> Length() const override;
  Status Close() override;
  Result<WriteResult> Metadata() override;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
  explicit PositionDeleteWriter(std::unique_ptr<Impl> impl);
};

//=============================================================================
// EqualityDeleteWriter
//=============================================================================

/// \brief Options for creating an EqualityDeleteWriter.
///
/// \note The following features from Java EqualityDeleteWriter are not yet supported:
/// - Encryption key metadata
/// - Metrics collection and reporting
/// - Split offsets tracking
struct ICEBERG_EXPORT EqualityDeleteWriterOptions {
  std::string path;
  std::shared_ptr<Schema> schema;
  std::shared_ptr<PartitionSpec> spec;
  PartitionValues partition;
  FileFormatType format = FileFormatType::kParquet;
  std::shared_ptr<FileIO> io;
  std::vector<int32_t> equality_field_ids;
  std::optional<int32_t> sort_order_id;
  std::shared_ptr<class WriterProperties> properties;
};

/// \brief Writer for Iceberg equality delete files.
class ICEBERG_EXPORT EqualityDeleteWriter : public FileWriter {
 public:
  static Result<std::unique_ptr<EqualityDeleteWriter>> Make(
      const EqualityDeleteWriterOptions& options);
  ~EqualityDeleteWriter() override;

  Status Write(ArrowArray* data) override;
  Result<int64_t> Length() const override;
  Status Close() override;
  Result<WriteResult> Metadata() override;

  const std::vector<int32_t>& equality_field_ids() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
  explicit EqualityDeleteWriter(std::unique_ptr<Impl> impl);
};

//=============================================================================
// FileWriterFactory
//=============================================================================

/// \brief Factory for creating Iceberg file writers.
///
/// \note Differences from Java FileWriterFactory:
/// - Java uses EncryptedOutputFile parameter, C++ uses path + FileIO
/// - C++ factory has state (schema, spec, io) configured once, reused for all writers
/// - Java FileWriterFactory is an interface, C++ is a concrete class with configuration
/// - C++ provides SetEqualityDeleteConfig() and SetPositionDeleteRowSchema() for customization
///
/// \warning This class is NOT thread-safe. Similar to the Java implementation,
/// FileWriterFactory does not provide internal synchronization. If multiple threads
/// need to create writers concurrently, either:
/// - Each thread should have its own FileWriterFactory instance, OR
/// - External synchronization must be used to protect access to the factory
///
/// Calling SetEqualityDeleteConfig() or SetPositionDeleteRowSchema() concurrently
/// with any New* method will result in undefined behavior due to data races.
class ICEBERG_EXPORT FileWriterFactory {
 public:
  FileWriterFactory(std::shared_ptr<Schema> schema, std::shared_ptr<PartitionSpec> spec,
                    std::shared_ptr<FileIO> io,
                    std::shared_ptr<class WriterProperties> properties = nullptr);
  ~FileWriterFactory();

  void SetEqualityDeleteConfig(std::shared_ptr<Schema> eq_delete_schema,
                                std::vector<int32_t> equality_field_ids);
  void SetPositionDeleteRowSchema(std::shared_ptr<Schema> pos_delete_row_schema);

  Result<std::unique_ptr<DataWriter>> NewDataWriter(
      std::string path, FileFormatType format, PartitionValues partition,
      std::optional<int32_t> sort_order_id = std::nullopt);

  Result<std::unique_ptr<PositionDeleteWriter>> NewPositionDeleteWriter(
      std::string path, FileFormatType format, PartitionValues partition);

  Result<std::unique_ptr<EqualityDeleteWriter>> NewEqualityDeleteWriter(
      std::string path, FileFormatType format, PartitionValues partition,
      std::optional<int32_t> sort_order_id = std::nullopt);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace iceberg

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

/// Writing encrypted tables.

#include <gtest/gtest.h>

#include "iceberg/catalog/memory/in_memory_catalog.h"
#include "iceberg/data/data_writer.h"
#include "iceberg/partition_spec.h"
#include "iceberg/schema.h"
#include "iceberg/sort_order.h"
#include "iceberg/table.h"
#include "iceberg/test/encryption_fixture.h"
#include "iceberg/test/matchers.h"
#include "iceberg/test/temp_file_test_base.h"
#include "iceberg/update/fast_append.h"
#include "iceberg/update/update_properties.h"

namespace iceberg {

class EncryptionWriteTest : public TempFileTestBase {
 protected:
  std::shared_ptr<Schema> schema_ = std::make_shared<Schema>(
      std::vector<SchemaField>{SchemaField::MakeRequired(1, "id", int64()),
                               SchemaField::MakeOptional(2, "data", string())},
      /*schema_id=*/0);
};

TEST_F(EncryptionWriteTest, WritesAreBlockedUntilSupported) {
  ICEBERG_UNWRAP_OR_FAIL(auto fixture, JavaEncryptedTableFixture::Load(2));

  // Data writers refuse the FileIO of an encrypted table
  EXPECT_THAT(DataWriter::Make({.path = CreateNewTempFilePath(),
                                .schema = schema_,
                                .spec = PartitionSpec::Unpartitioned(),
                                .partition = PartitionValues{},
                                .format = FileFormatType::kParquet,
                                .io = fixture.io}),
              IsError(ErrorKind::kNotSupported));

  // Snapshot updates refuse encrypted tables
  auto warehouse = CreateNewTempFilePath();
  std::filesystem::create_directories(warehouse + "/t/metadata");
  std::shared_ptr<FileIO> io(arrow::MakeLocalFileIO());
  ICEBERG_UNWRAP_OR_FAIL(auto catalog, InMemoryCatalog::Make("c", io, warehouse, {}));
  TableIdentifier ident{.name = "t"};
  ICEBERG_UNWRAP_OR_FAIL(
      auto table, catalog->CreateTable(ident, schema_, PartitionSpec::Unpartitioned(),
                                       SortOrder::Unsorted(), warehouse + "/t", {}));
  ICEBERG_UNWRAP_OR_FAIL(auto props, table->NewUpdateProperties());
  props->Set("format-version", "3");
  props->Set("encryption.key-id", "keyA");
  ASSERT_THAT(props->Commit(), IsOk());
  ICEBERG_UNWRAP_OR_FAIL(table, catalog->LoadTable(ident));

  ICEBERG_UNWRAP_OR_FAIL(auto append, table->NewFastAppend());
  EXPECT_THAT(append->Commit(), IsError(ErrorKind::kNotSupported));
}

}  // namespace iceberg

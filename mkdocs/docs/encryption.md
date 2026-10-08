<!--
  ~ Licensed to the Apache Software Foundation (ASF) under one
  ~ or more contributor license agreements.  See the NOTICE file
  ~ distributed with this work for additional information
  ~ regarding copyright ownership.  The ASF licenses this file
  ~ to you under the Apache License, Version 2.0 (the
  ~ "License"); you may not use this file except in compliance
  ~ with the License.  You may obtain a copy of the License at
  ~
  ~   http://www.apache.org/licenses/LICENSE-2.0
  ~
  ~ Unless required by applicable law or agreed to in writing,
  ~ software distributed under the License is distributed on an
  ~ "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
  ~ KIND, either express or implied.  See the License for the
  ~ specific language governing permissions and limitations
  ~ under the License.
-->

# Table Encryption

iceberg-cpp reads and writes tables encrypted with the Iceberg table encryption
scheme of the Java implementation (`StandardEncryptionManager`). Tables encrypted
by Java can be read by C++ and the other way around.

Encryption protects data, delete, manifest and manifest list files. Data and delete
files in Parquet use Parquet modular encryption (encrypted footer, `PARE` magic);
manifests, manifest lists, Avro data files and Puffin deletion vectors are AES GCM
Stream files (`AGS1` magic, see the
[spec](https://github.com/apache/iceberg/blob/main/format/gcm-stream-spec.md)).
`metadata.json` is not encrypted.

## Build

Encryption is enabled by the `ICEBERG_ENCRYPTION` CMake option (`ON` by default) of
the bundle library. It requires OpenSSL and builds the vendored Arrow with Parquet
encryption. Call `iceberg::encryption::RegisterAll()` (from
`iceberg/encryption/encryption_register.h`) before creating catalogs if the static
registration of the bundle library is not linked in.

## Configuration

The property names are those of Java.

| Level | Property | Description |
|---|---|---|
| Catalog | `encryption.kms-type` | Built-in KMS client: `aws`, `gcp` or `azure` |
| Catalog | `encryption.kms-impl` | Name of a KMS client registered with `KmsRegistry::Register` |
| Table | `encryption.key-id` | ID of the table master key in the KMS; enables encryption |
| Table | `encryption.data-key-length` | Length of data keys: 16 (default), 24 or 32 bytes |

Encryption requires table format version 3. The table key ID cannot be changed or
removed once set.

```cpp
// A custom key management client
class MyKms : public iceberg::KeyManagementClient {
  // WrapKey / UnwrapKey with master keys kept in your KMS
};
iceberg::KmsRegistry::Register("my-kms", [] { return std::make_unique<MyKms>(); });

// Catalogs create the client from their properties
auto catalog = iceberg::InMemoryCatalog::Make(
    "catalog", file_io, warehouse, {{"encryption.kms-impl", "my-kms"}});
```

Tables loaded from a catalog with a KMS client have an `EncryptingFileIO`; scans,
writers and commits use it to encrypt and decrypt files. A catalog without a KMS
client can load an encrypted table but cannot read or commit to it.

## Security

- The table key ID must come from a trusted source. Catalogs that keep a copy apart
  from `metadata.json` pass it to `EncryptionUtil::MakeTableFileIO`, which rejects
  metadata with a different key ID.
- The REST catalog creates its KMS client from the client-side catalog properties
  only, never from configuration returned by the server.
- `InMemoryKms` keeps master keys in memory and is meant for tests only.

## Not supported yet

Following the Java implementation, these are not supported yet: column-level
encryption, encrypted table and partition statistics files, removing unused keys
when snapshots expire, and re-wrapping keys with a new master key. The Hive catalog
does not support table operations yet.

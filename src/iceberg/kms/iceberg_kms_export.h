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

/// \file iceberg/kms/iceberg_kms_export.h
/// \brief Define symbol visibility macros for the cloud KMS client library.

#if defined(_WIN32) || defined(__CYGWIN__)
#  ifdef ICEBERG_KMS_STATIC
#    define ICEBERG_KMS_EXPORT
#  elif defined(ICEBERG_KMS_EXPORTING)
#    define ICEBERG_KMS_EXPORT __declspec(dllexport)
#  else
#    define ICEBERG_KMS_EXPORT __declspec(dllimport)
#  endif
#else  // Not Windows
#  ifndef ICEBERG_KMS_EXPORT
#    define ICEBERG_KMS_EXPORT __attribute__((visibility("default")))
#  endif
#endif

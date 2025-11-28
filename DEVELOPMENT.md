# Development Guide

This guide documents coding standards, best practices, and common review feedback patterns for the Apache Iceberg C++ project. Following these guidelines will help ensure your contributions are accepted quickly.

## Table of Contents

- [Code Style and Formatting](#code-style-and-formatting)
- [Error Handling](#error-handling)
- [Testing Requirements](#testing-requirements)
- [API Design Patterns](#api-design-patterns)
- [Documentation](#documentation)
- [Build System](#build-system)
- [C++ Best Practices](#c-best-practices)
- [Project-Specific Conventions](#project-specific-conventions)
- [Pre-commit Checklist](#pre-commit-checklist)

## Code Style and Formatting

### Header Organization

**Use forward declarations to reduce compilation time:**
```cpp
// Good: In public header, use forward declaration
#include "iceberg/type_fwd.h"

// Bad: Including full header when forward declaration suffices
#include "iceberg/type.h"
```

**Minimize header dependencies in public APIs:**
- Don't include internal headers in public API headers
- Use the Pimpl idiom for implementation details if necessary

**Export macros are required for all public APIs:**
```cpp
class ICEBERG_EXPORT MyPublicClass {
  // ...
};

ICEBERG_EXPORT Result<Table> LoadTable(const std::string& name);
```

**Module-specific export macros:**
- Use `ICEBERG_REST_EXPORT` for REST catalog module
- Use `ICEBERG_EXPORT` for core library

### Naming Conventions

**Private member variables use trailing underscore:**
```cpp
class MyClass {
 private:
  std::string field_;
  int count_;
};
```

**Constants use `k` prefix:**
```cpp
constexpr int kInitialSpecId = 0;
constexpr std::string_view kHexChars = "0123456789abcdef";
```

**Operator overload parameters:**
```cpp
bool operator==(const MyType& lhs, const MyType& rhs);
```

### File Organization

**Alphabetical sorting is required in:**
- CMakeLists.txt source file lists
- Include statements (where practical)
- Enum values in switch statements (list all explicitly, no default)

**Example CMakeLists.txt:**
```cmake
set(SOURCES
  catalog.cc
  expression.cc
  schema.cc
  table.cc
)
```

**Anonymous namespaces:**
- Place at top or bottom of implementation files for readability
- Don't scatter throughout the file

**Keep build files in sync:**
- Update both `CMakeLists.txt` and `meson.build` when adding files
- Add public headers to `install_headers` in `meson.build`

## Error Handling

### Result Type Usage

**Prefer `Result<T>` over exceptions for expected errors:**
```cpp
// Good: Factory method returns Result
static Result<std::unique_ptr<Schema>> Make(std::vector<Field> fields);

// Good: Validation method returns Status
Status Validate() const;
```

**Use the error collection pattern for multiple errors:**
```cpp
std::vector<Error> errors;
if (!ValidateField1()) errors.push_back(Error::InvalidArgument("..."));
if (!ValidateField2()) errors.push_back(Error::InvalidArgument("..."));
if (!errors.empty()) {
  return Error::InvalidArgument(FormatErrors(errors));
}
```

### Error Messages

**Be specific - include actual values:**
```cpp
// Good
return Error::InvalidArgument(
    std::format("Invalid UUID: expected 36 characters, got {}", uuid.size()));

// Bad
return Error::InvalidArgument("Invalid UUID");
```

**Use appropriate error kinds:**
```cpp
Error::InvalidArgument(...)  // Invalid input parameters
Error::NotSupported(...)     // Feature not implemented
Error::NotImplemented(...)   // Planned feature, not yet done
Error::IOError(...)          // File/network I/O errors
```

### Validation

**Validate inputs early:**
```cpp
Result<X> Factory::Make(const std::string& input) {
  if (input.empty()) {
    return Error::InvalidArgument("Input cannot be empty");
  }
  if (input.size() > kMaxSize) {
    return Error::InvalidArgument(
        std::format("Input too large: {} > {}", input.size(), kMaxSize));
  }
  // ... rest of implementation
}
```

**Mark error paths with `[[unlikely]]`:**
```cpp
if (ptr == nullptr) [[unlikely]] {
  return Error::InvalidArgument("Null pointer");
}
```

**Compare with Java implementation:**
- Always verify error handling behavior matches the Java reference implementation
- Match Java's handling of edge cases, null values, and validation

## Testing Requirements

### Test Coverage

**Always test both success and error cases:**
```cpp
TEST(MyTest, ValidInput) {
  auto result = Factory::Make("valid");
  EXPECT_THAT(result, IsOK());
}

TEST(MyTest, InvalidInput) {
  auto result = Factory::Make("");
  EXPECT_THAT(result, IsError(ErrorKind::kInvalidArgument));
  EXPECT_THAT(result, HasErrorMessage("Input cannot be empty"));
}
```

**Use parameterized tests for similar cases:**
```cpp
class MyTestFixture : public testing::TestWithParam<std::tuple<std::string, int>> {};

TEST_P(MyTestFixture, ProcessInput) {
  auto [input, expected] = GetParam();
  // ...
}

INSTANTIATE_TEST_SUITE_P(
    ValidCases,
    MyTestFixture,
    testing::Values(
        std::make_tuple("case1", 1),
        std::make_tuple("case2", 2)
    )
);
```

**Use test matchers:**
```cpp
EXPECT_THAT(result, IsOK());
EXPECT_THAT(result, IsError(ErrorKind::kInvalidArgument));
EXPECT_THAT(result, HasErrorMessage(testing::HasSubstr("expected substring")));
```

### Test Organization

**Keep tests with related code:**
- Don't create separate test directories unless absolutely necessary
- Test files live in `src/iceberg/test/`

**Add test helper utilities:**
```cpp
// Helper for common JSON deserialization patterns
template <typename T>
Result<T> FromJsonHelper(const std::string& json_str) {
  // ...
}
```

**Alphabetize test files in CMakeLists.txt:**
```cmake
set(TEST_SOURCES
  expression_test.cc
  schema_test.cc
  table_test.cc
)
```

## API Design Patterns

### Factory Methods

**Make constructors private/protected, use factory methods:**
```cpp
class Schema {
 public:
  static Result<std::shared_ptr<Schema>> Make(std::vector<Field> fields);

 private:
  Schema(std::vector<Field> fields) : fields_(std::move(fields)) {}

  std::vector<Field> fields_;
};
```

**Use builder pattern for complex objects:**
```cpp
class TableMetadataBuilder {
 public:
  TableMetadataBuilder& SetLocation(std::string location) {
    location_ = std::move(location);
    return *this;
  }

  TableMetadataBuilder& SetSchema(std::shared_ptr<Schema> schema) {
    schema_ = std::move(schema);
    return *this;
  }

  Result<std::shared_ptr<TableMetadata>> Build();

 private:
  std::string location_;
  std::shared_ptr<Schema> schema_;
};
```

**Error handling in builders:**
```cpp
// Option 1: Postpone errors until Build() (preferred)
Result<std::shared_ptr<T>> Build() {
  std::vector<Error> errors;
  if (!field_) errors.push_back(Error::InvalidArgument("Missing field"));
  // ...
  if (!errors.empty()) return Error::InvalidArgument(FormatErrors(errors));
  return std::make_shared<T>(...);
}

// Option 2: Track errors in builder state
class Builder {
  std::vector<Error> errors_;
  // Methods add to errors_ vector
};
```

### Memory Management

**Smart pointer usage guidelines:**
```cpp
// Use shared_ptr for objects that need shared ownership
std::shared_ptr<Schema> schema;
std::shared_ptr<TableMetadata> metadata;

// Use unique_ptr for exclusive ownership
std::unique_ptr<Reader> reader;

// Accept const shared_ptr& when function needs to hold a reference
void SetSchema(const std::shared_ptr<Schema>& schema) {
  schema_ = schema;
}

// Accept const T& when function doesn't need ownership
Status ValidateSchema(const Schema& schema) {
  // ...
}
```

**Use move semantics appropriately:**
```cpp
// Move large objects and unique_ptr
builder.SetSchema(std::move(schema));
return std::move(unique_ptr_result);

// Use rvalue references to clarify ownership transfer
void SetName(std::string&& name) {
  name_ = std::move(name);
}
```

### Const Correctness

**Mark methods const if they don't modify state:**
```cpp
class Table {
 public:
  std::shared_ptr<Schema> schema() const { return schema_; }
  Transaction NewTransaction() const;

 private:
  std::shared_ptr<Schema> schema_;
};
```

**Use const references for read-only parameters:**
```cpp
Result<bool> Equals(const Schema& left, const Schema& right);
```

**Prefer empty containers over `std::optional` when practical:**
```cpp
// Good: Empty map means no properties
std::unordered_map<std::string, std::string> properties;

// Less ideal: Optional forces caller to check
std::optional<std::unordered_map<std::string, std::string>> properties;
```

## Documentation

### Comment Style

**Use Doxygen format for public APIs:**
```cpp
/// \brief Creates a new schema with the given fields.
///
/// \param fields The fields to include in the schema
/// \return A Result containing the schema or an error
///
/// \note Fields must have unique IDs within the schema
static Result<std::shared_ptr<Schema>> Make(std::vector<Field> fields);
```

**Comment format:**
- `///` for API documentation
- `//` for implementation comments
- `\brief` for class/function summaries
- `\param` for each parameter
- `\return` for return values
- `\note` for important details

### Documentation Content

**Focus on "why" over "what":**
```cpp
// Good: Explains the rationale
// We use lazy initialization here because schema conversion is expensive
// and not always needed.

// Bad: Just repeats the code
// This function initializes the schema lazily.
```

**Override functions generally don't need comments:**
```cpp
// No comment needed - behavior is clear from base class
Status Read() override;
```

**Use TODO comments with context:**
```cpp
// TODO(username): Add support for nested structs after we implement
// the StructAccessor interface in PR #123
```

### Self-Explanatory Code

**Use named parameters for clarity:**
```cpp
auto result = Factory::Make(
    /*field_id=*/1,
    /*name=*/"my_field",
    /*required=*/true);
```

## Build System

### CMake/Meson Patterns

**Library dependency conventions:**
```cmake
# Static libraries depend on iceberg_static
add_library(my_static_lib STATIC ${SOURCES})
target_link_libraries(my_static_lib PUBLIC iceberg_static)

# Shared libraries depend on iceberg_shared
add_library(my_shared_lib SHARED ${SOURCES})
target_link_libraries(my_shared_lib PUBLIC iceberg_shared)
```

**Compiler flags:**
```python
# Check flag support before using
supported_flags = []
for flag in ['-Wextra', '-Wpedantic']:
    if cpp.has_argument(flag):
        supported_flags += flag

# Apply flags only where needed
my_lib = library('my_lib',
    cpp_args: supported_flags,
    gnu_symbol_visibility: 'inlineshidden',
)
```

**Optional features:**
```cmake
option(ICEBERG_BUILD_REST "Build REST catalog support" ON)

if(ICEBERG_BUILD_REST)
  add_subdirectory(rest)
endif()
```

### Dependency Management

**Prefer system packages over vendored:**
```cmake
include(FetchContent)
FetchContent_Declare(
  mydep
  URL https://example.com/mydep-1.2.3.tar.gz
  FIND_PACKAGE_ARGS 1.2.3  # Try find_package first
)
FetchContent_MakeAvailable(mydep)
```

**Use official release tarballs:**
```cmake
# Good: Official release tarball
URL https://github.com/foo/bar/releases/download/v1.2.3/bar-1.2.3.tar.gz

# Bad: Git tag (slower, requires git)
GIT_TAG v1.2.3
```

### CI/CD

**Set up pre-commit hooks:**
```bash
pip install pre-commit
pre-commit install
pre-commit run --all-files
```

**Test on multiple platforms:**
- Linux (Ubuntu with gcc-14)
- macOS (latest)
- Windows (MSVC)

**Test with multiple compilers:**
- GCC 11+
- Clang 16+
- MSVC 19.30+

## C++ Best Practices

### Modern C++ (C++23)

This project uses **C++23**. Take advantage of modern features:

```cpp
// std::expected for Result type
std::expected<int, Error> result = GetValue();

// Enhanced ranges and views
auto zipped = std::views::zip(values, indices);

// std::unreachable for impossible paths
switch (kind) {
  case Kind::kA: return HandleA();
  case Kind::kB: return HandleB();
}
std::unreachable();

// std::format for string formatting
std::format("Value: {}, Count: {}", value, count);
```

### Template Usage

**Define concepts for template constraints:**
```cpp
template <typename T>
concept TermType = std::is_base_of_v<Term, T>;

template <TermType T>
Result<BoundTerm> Bind(const T& term);
```

**Use conditional types:**
```cpp
template <typename T>
using ResultType = std::conditional_t<
    std::is_void_v<T>,
    Status,
    Result<T>>;
```

### Performance Considerations

**Reserve capacity for vectors:**
```cpp
std::vector<Field> fields;
fields.reserve(expected_count);  // Avoid reallocations
for (const auto& item : items) {
  fields.push_back(ProcessItem(item));
}
```

**Use string_view for read-only strings:**
```cpp
// Avoids unnecessary copies
bool IsValidName(std::string_view name) {
  return !name.empty() && std::isalpha(name[0]);
}
```

**Explicit move when transferring ownership:**
```cpp
builder.SetField(std::move(large_object));
return std::move(unique_ptr_result);
```

### Safety

**Use checked casts:**
```cpp
// Don't use static_cast directly
auto* derived = internal::checked_cast<DerivedType*>(base);
```

**DCHECK vs runtime checks:**
```cpp
// Debug-only assertion - use for internal invariants
ICEBERG_DCHECK(ptr != nullptr);

// Runtime check - use for user-facing errors
if (ptr == nullptr) {
  return Error::InvalidArgument("Null pointer");
}
```

## Project-Specific Conventions

### Namespace Usage

**Remove redundant namespace prefixes inside class scope:**
```cpp
// Good
class MyClass {
  static Result<BoundPredicate> Make(...) {
    return BoundPredicate::Create(...);
  }
};

// Bad - redundant namespace
class MyClass {
  static Result<BoundPredicate> Make(...) {
    return iceberg::BoundPredicate::Create(...);
  }
};
```

### Constants

**Define constants instead of magic values:**
```cpp
// Good
constexpr int kDefaultSpecId = 0;
constexpr std::string_view kMetadataFile = "metadata.json";

// Bad
if (spec_id == 0) { ... }
if (filename == "metadata.json") { ... }
```

**Use proper types for constants:**
```cpp
// Good: string_view for string constants
constexpr std::string_view kFieldName = "my_field";

// Bad: C-style string
#define FIELD_NAME "my_field"
```

### JSON Serialization

**Consistent method naming:**
```cpp
class MyType {
 public:
  Result<std::string> ToJson() const;
  static Result<MyType> FromJson(const std::string& json);
};
```

**Use JSON utilities:**
```cpp
// Reading fields
auto value = GetJsonValue<std::string>(json, "field_name");
auto optional_value = GetJsonValueOptional<int>(json, "optional_field");

// Writing fields
SetJsonField(json, "field_name", value);
SetContainerField(json, "array_field", array_values);
```

**Match REST API specification exactly:**
- Field names must match spec (case-sensitive)
- Optional fields must be handled correctly
- Types must match (string vs int vs bool)

### Catalog/REST Module

**Separate endpoint construction from logic:**
```cpp
// Good: Utility function for endpoint paths
std::string NamespacePath(const Namespace& ns) {
  return std::format("/namespaces/{}", ns.ToString());
}

// Use in catalog implementation
auto response = http_client_->Get(NamespacePath(namespace));
```

**HTTP client abstraction:**
- Design library-independent interface
- Don't leak CPR or other HTTP library details into public API

**Configuration pattern:**
```cpp
class CatalogConfig : public ConfigBase {
 public:
  std::string uri() const { return GetProperty("uri"); }
  std::optional<std::string> credential() const {
    return GetPropertyOptional("credential");
  }
};
```

### Table Metadata

**Immutability:**
```cpp
// TableMetadata is read-only after construction
class TableMetadata {
 public:
  // No setters - use TableMetadataBuilder for changes
  std::shared_ptr<Schema> schema() const { return schema_; }

 private:
  // Constructor is private
  TableMetadata(...);

  std::shared_ptr<Schema> schema_;
};
```

**Builder pattern with validation:**
```cpp
class TableMetadataBuilder {
 public:
  TableMetadataBuilder& SetSchema(std::shared_ptr<Schema> schema);

  Result<std::shared_ptr<TableMetadata>> Build() {
    RETURN_IF_ERROR(Validate());
    return std::make_shared<TableMetadata>(...);
  }

 private:
  Status Validate() const;
};
```

## Pre-commit Checklist

Before submitting a PR, ensure:

- [ ] **Code formatting**: Run `pre-commit run --all-files`
- [ ] **Tests pass**: Both success and error cases covered
- [ ] **Build files updated**: Both `CMakeLists.txt` and `meson.build` if needed
- [ ] **Export macros added**: `ICEBERG_EXPORT` on public APIs
- [ ] **Headers organized**: Forward declarations used where possible
- [ ] **Alphabetical ordering**: Source lists, includes (where practical)
- [ ] **Java compatibility checked**: Behavior matches reference implementation
- [ ] **Error handling**: Specific error messages with actual values
- [ ] **Documentation**: Public APIs have Doxygen comments
- [ ] **Input validation**: Early checks with appropriate error kinds
- [ ] **Memory management**: Smart pointers used correctly
- [ ] **Const correctness**: Methods and parameters marked const appropriately

## Common Review Feedback Themes

Based on analysis of past PRs, reviewers frequently comment on:

1. **"Why not use X from Java?"** - Always check the Java implementation for reference
2. **"Can we simplify this?"** - Prefer simpler, more readable code
3. **"Add tests for invalid cases"** - Error paths must be tested
4. **"Use forward declaration"** - Reduce header dependencies
5. **"Add comments"** - Explain non-obvious behavior
6. **"Alphabetically sort"** - Maintain consistency in ordering
7. **"Remove redundant"** - Eliminate unnecessary code/includes
8. **"Match spec exactly"** - REST API types must match specification precisely

## Resources

- [Apache Iceberg Specification](https://iceberg.apache.org/spec/)
- [Apache Iceberg Java Implementation](https://github.com/apache/iceberg)
- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
- [Project Issue Tracker](https://github.com/apache/iceberg-cpp/issues)

## Getting Help

- Ask questions in PR comments
- Review similar merged PRs for examples
- Refer to this guide for common patterns
- Check the Java implementation for reference behavior

---

*This guide is based on analysis of review feedback from 100+ merged pull requests. It reflects the collective wisdom of the Apache Iceberg C++ community.*

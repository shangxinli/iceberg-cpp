# Apache Iceberg C++ - Claude Instructions

**CRITICAL: Before writing or reviewing any code, always follow the coding standards documented in `DEVELOPMENT.md`.**

## Core Principles

When writing or reviewing code for this project:

1. **Always check the Java reference implementation** at https://github.com/apache/iceberg for behavior compatibility
2. **Follow the patterns in DEVELOPMENT.md** - it contains lessons learned from 100+ code reviews
3. **Test both success and error cases** - every PR must include error path testing
4. **Be specific in error messages** - include actual values, not just what failed

## Code Writing Checklist

Before making any code changes:

### Error Handling
- [ ] Use `Result<T>` for expected errors (not exceptions)
- [ ] Factory methods return `Result<std::unique_ptr<T>>`
- [ ] Validation methods return `Status`
- [ ] Use error collection pattern for multiple errors: `std::vector<Error>`
- [ ] Error messages include actual values: `std::format("Expected {}, got {}", expected, actual)`
- [ ] Use appropriate error kinds: `InvalidArgument`, `NotSupported`, `IOError`, etc.
- [ ] Validate inputs early with `[[unlikely]]` on error paths

### API Design
- [ ] Make constructors private/protected, use factory methods (`Make()`)
- [ ] Use builder pattern for objects with many parameters
- [ ] Add `ICEBERG_EXPORT` macro to all public classes and functions
- [ ] Use `const std::shared_ptr<T>&` when function needs to hold reference
- [ ] Use `const T&` when function doesn't need ownership
- [ ] Mark methods `const` if they don't modify state

### Code Style
- [ ] Private members use trailing underscore: `field_`, `schema_`
- [ ] Constants use `k` prefix: `kDefaultSpecId`, `kMaxSize`
- [ ] Use forward declarations in headers where possible
- [ ] Alphabetically sort: CMakeLists.txt sources, includes (where practical)
- [ ] Update both `CMakeLists.txt` and `meson.build` when adding files

### Testing
- [ ] Test both valid and invalid inputs
- [ ] Use `EXPECT_THAT(result, IsOK())` and `EXPECT_THAT(result, IsError(ErrorKind::...))`
- [ ] Use `EXPECT_THAT(result, HasErrorMessage(...))` to verify error messages
- [ ] Add parameterized tests (`TEST_P`) for similar test cases
- [ ] Add test files alphabetically to CMakeLists.txt

### Documentation
- [ ] Add Doxygen comments to public APIs: `\brief`, `\param`, `\return`, `\note`
- [ ] Focus on "why" not "what" in comments
- [ ] Use named parameters for clarity: `/*field_id=*/1`
- [ ] Use `TODO(username): ...` format with context

### Memory & Performance
- [ ] Use `std::shared_ptr` for shared ownership (Schema, TableMetadata)
- [ ] Use `std::unique_ptr` for exclusive ownership
- [ ] Call `.reserve()` on vectors before filling
- [ ] Use `std::string_view` for read-only string parameters
- [ ] Explicitly `std::move` when transferring ownership

## Code Review Checklist

When reviewing code:

### Common Issues to Check
- [ ] **Java compatibility**: Does behavior match Java Iceberg implementation?
- [ ] **Error cases tested**: Are invalid inputs tested, not just happy paths?
- [ ] **Forward declarations**: Can any `#include` be replaced with forward declaration?
- [ ] **Alphabetical sorting**: Are additions sorted alphabetically?
- [ ] **Simplified code**: Can it be simpler or more readable?
- [ ] **Redundant code**: Any unnecessary includes, checks, or code?
- [ ] **Export macros**: Are `ICEBERG_EXPORT` macros present on public APIs?
- [ ] **Const correctness**: Should methods/parameters be marked `const`?
- [ ] **Smart pointers**: Are they used appropriately (shared vs unique)?
- [ ] **Error messages**: Are they specific with actual values?
- [ ] **Comments**: Do public APIs have Doxygen documentation?
- [ ] **Build files**: Are both CMakeLists.txt and meson.build updated?
- [ ] **REST spec**: Do REST types match the specification exactly?

### Project-Specific Patterns

**Error Collection Pattern:**
```cpp
std::vector<Error> errors;
if (!ValidateField1()) errors.push_back(Error::InvalidArgument("..."));
if (!ValidateField2()) errors.push_back(Error::InvalidArgument("..."));
if (!errors.empty()) {
  return Error::InvalidArgument(FormatErrors(errors));
}
```

**Factory Pattern:**
```cpp
class MyClass {
 public:
  static Result<std::unique_ptr<MyClass>> Make(params...);
 private:
  MyClass(params...);  // Constructor is private
};
```

**Builder Pattern:**
```cpp
class Builder {
 public:
  Builder& SetField(Type value) {
    field_ = std::move(value);
    return *this;
  }
  Result<std::shared_ptr<T>> Build();
};
```

**JSON Serialization:**
```cpp
class MyType {
 public:
  Result<std::string> ToJson() const;
  static Result<MyType> FromJson(const std::string& json);
};
```

## Critical Review Feedback Themes

Based on past reviews, watch out for:

1. **"Why not use X from Java?"** - Check Java implementation first
2. **"Can we simplify this?"** - Prefer simpler code
3. **"Add tests for invalid cases"** - Error paths must be tested
4. **"Use forward declaration"** - Reduce header dependencies
5. **"Add comments"** - Explain non-obvious behavior
6. **"Alphabetically sort"** - Maintain consistency
7. **"Remove redundant"** - Eliminate unnecessary code
8. **"Match spec exactly"** - REST API must match spec precisely

## Pre-commit Commands

Before submitting any PR:
```bash
pre-commit run --all-files  # Format and lint
ctest                        # Run all tests
```

## References

- **Full coding guide**: `DEVELOPMENT.md`
- **Iceberg spec**: https://iceberg.apache.org/spec/
- **Java implementation**: https://github.com/apache/iceberg
- **REST API spec**: https://iceberg.apache.org/rest/

---

**Remember: This project uses C++23 and follows strict Apache project standards. Every code change should match the Java reference implementation behavior and pass both success and error case tests.**

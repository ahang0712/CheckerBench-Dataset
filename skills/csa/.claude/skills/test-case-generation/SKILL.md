---
name: test-case-generation
description: Use when debugging why a checker does not fire or before running full project scans — create minimal standalone C test cases that isolate the bug pattern from complex project headers, verify the checker distinguishes buggy from fixed code, and cover edge cases.
---

# Test Case Generation for CSA Checkers

## Why create test cases

- Isolate the bug pattern from complex project headers
- Quickly verify checker before running full scans
- Confirm checker distinguishes buggy from fixed code

## Template

```c
// Standalone test for checker validation
typedef <types_needed>;

// Define the struct(s) involved
typedef struct {
    <field_type> <field_name>;
    ...
} StructName;

// Buggy version (triggers the checker)
void test_buggy() {
    StructName *obj = <allocation>;
    <buggy_code_path>;
}

// Fixed version (should NOT trigger the checker)
void test_fixed() {
    StructName *obj = <allocation>;
    <fixed_code_path>;
}
```

## Running the Test

```bash
cat > /tmp/test.c << 'EOF'
...test code...
EOF

/opt/llvm/bin/clang --analyze \
  -Xanalyzer -load -Xanalyzer checker.so \
  -Xanalyzer -analyzer-checker="checker_name" \
  /tmp/test.c 2>&1 | grep -c "[checker_name]"
```

## Edge Cases to Cover

- Multiple instances of the pattern in one function
- Nested struct access chains
- Pattern in different functions
- Pattern behind conditional branches (#ifdef, if/else)

---
name: c-code-analysis
description: Use when analysing C source code to understand a bug pattern deeply — trace data flow from source to sink, navigate struct hierarchies and pointer chains, and identify taint sources. Helps map concrete code to the abstract vulnerability class.
---

# C Code Analysis for Checker Synthesis

## Data Flow Tracing

When analysing a patch, trace the full data flow of the affected value:

1. **Source**: Where does the value originate? (function parameter, struct field, global, malloc, network read?)
2. **Transformations**: What operations modify it? (arithmetic, bit shifts, type casts, macro expansion?)
3. **Sinks**: Where is it consumed? (passed to function, compared, dereferenced, used in allocation size?)

## Struct Hierarchy Navigation

For complex C structs with nested pointers:
```bash
# Find struct definition
grep -rn "struct.*target_type" <repo>/include/
# Trace field types
grep -rn "->field_name" <source_dir>
```

## Pointer Chain Analysis

When analysing `ptr->a->b->c`:
1. Check each link for NULL before analysis
2. Identify which link the patch adds a check for
3. Look for all other dereferences of the same chain

## Taint Source Identification

Common taint sources in C code:
- Network reads (`recv`, `read`, custom `stream_read_*`)
- File reads (`fread`, `read`, `mmap`)
- User input parameters in API functions
- Untrusted struct fields passed from callers

## Key Commands
```bash
# Find all assignments to a field
grep -rn "->field_name\s*=" <source_dir>
# Find all uses of a field
grep -rn "->field_name" <source_dir>
# Find function definitions
grep -rn "^static.*function_name\|^GF_Err.*function_name" <source_dir>
```

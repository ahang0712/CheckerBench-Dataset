# CWE-121: Stack-based Buffer Overflow
- **Typical pattern:** Writing beyond the bounds of a stack-allocated buffer (local variable or parameter) via unsafe functions such as `strcpy`, `gets`, `sprintf`, or `memcpy` with an attacker-controlled size. The buffer lives on the stack, so a write overflow can corrupt the return address or other local variables.
- **Checker suggestion:** Use taint-based analysis to track data from untrusted sources (argv, network, file) into stack-buffer sink functions; flag any call where the copy size is not bounded by a check against the buffer's declared size.

## CWE-122: Heap-based Buffer Overflow
- **Typical pattern:** Writing past the end of a dynamically allocated buffer (malloc/calloc/realloc) using `strcpy`, `memcpy`, or similar, often because the allocation size was miscalculated or a tainted length was used without validation.
- **Checker suggestion:** Model the allocated size of each heap buffer and compare it against the number of bytes written. Flag any write operation (strcpy, memcpy, user loop) where the source may exceed the destination allocation.

## CWE-1284: Improper Validation of Specified Quantity in Input
- **Typical pattern:** The product reads a quantity field (length, count, size) from external input and uses it to allocate memory, control a loop, or index an array without validating that the quantity is non-negative, within expected bounds, or consistent with the actual data.
- **Checker suggestion:** Flag tainted integer values flowing into allocation-size arguments or loop-condition comparisons without prior range validation (e.g., `if (x < 0)` or `if (x > MAX)` guards).

## CWE-131: Incorrect Calculation of Buffer Size
- **Typical pattern:** The buffer size passed to `malloc`/`calloc`/`realloc` is computed incorrectly — forgetting the null terminator (`+1`), using `sizeof(pointer)` instead of `sizeof(array)`, or allowing integer overflow/wraparound in the arithmetic that computes the byte count.
- **Checker suggestion:** Examine arithmetic expressions inside allocation arguments for overflow-prone patterns (multiply without overflow check, addition without sentinel), off-by-one errors, and misuse of `sizeof` on pointer types.

## CWE-763: Release of Invalid Pointer or Reference
- **Typical pattern:** Calling `free()` on a pointer that was not returned by a malloc-family function, or that no longer points to the start of an allocated region (e.g., after pointer arithmetic), or mismatching allocator/deallocator pairs (e.g., `new` / `free` or `malloc` / `delete`).
- **Checker suggestion:** Track pointer provenance from allocation to deallocation. Flag any `free()` call where the argument may trail the original allocation pointer, or where the deallocation routine does not match the allocation routine.

## CWE-824: Access of Uninitialized Pointer
- **Typical pattern:** A pointer variable is declared but not assigned a valid address before it is dereferenced or passed to `free()`. This often arises on error paths where initialization was skipped, or in struct/union members that are not fully initialized.
- **Checker suggestion:** Perform per-path dataflow analysis to detect dereferences of pointers that may be undefined on some execution path. Flag `free()` of a pointer whose definition is not reachable on all paths to the call site.

## CWE-843: Access of Resource Using Incompatible Type ('Type Confusion')
- **Typical pattern:** A memory region is allocated or initialized as one type (e.g., a struct via a union's first member, or a base class object) but later accessed as an incompatible type (e.g., a different union member, or a derived class via an incorrect cast), leading to out-of-bounds reads/writes.
- **Checker suggestion:** Flag `reinterpret_cast` or C-style casts between pointer types that point to objects of unrelated types or different sizes, and flag union accesses where the active member is ambiguous or inconsistent across the control flow.
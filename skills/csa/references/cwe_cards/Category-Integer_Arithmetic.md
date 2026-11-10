# CWE-129: Improper Validation of Array Index
- **Typical pattern:** An untrusted integer is used directly as an array index without checking both lower and upper bounds. Common in C/C++ where signed indices are only compared against an upper limit (e.g., `if (index < len)`), allowing negative values or values exceeding array capacity.
- **Checker suggestion:** Use a checker that enforces a two-sided range guard (`index >= 0 && index < len`) on all tainted integer values before they are used as array subscripts. Flag any array access where the index originates from an unchecked external source.

## CWE-191: Integer Underflow (Wrap or Wraparound)
- **Typical pattern:** A subtraction operation produces a value below the type's minimum, wrapping to a large positive number (unsigned) or INT_MIN (signed). Often seen in length/size calculations where an attacker-controlled value is subtracted from a small positive value.
- **Checker suggestion:** Insufficient samples, no reliable recommendation yet.

## CWE-193: Off-by-one Error
- **Typical pattern:** A loop condition uses `<=` instead of `<`, or a buffer size calculation forgets to reserve space for a null terminator, causing access one element past the allocated array. Frequently found in C string manipulation (strncpy, strncat, sprintf with fixed-width formats).
- **Checker suggestion:** Insufficient samples, no reliable recommendation yet.

## CWE-681: Incorrect Conversion between Numeric Types
- **Typical pattern:** A wider integer (e.g., `long long` or `size_t`) is implicitly truncated to a narrower type (e.g., `int` or `unsigned short`), or a signed value is cast to an unsigned type producing an unexpectedly large positive result. Often chains into buffer overflows or excessive allocation.
- **Checker suggestion:** Flag all narrowing or sign-changing implicit conversions that could lose data; require explicit range-checked casting wrappers for any tainted numeric value crossing type boundaries.

## CWE-682: Incorrect Calculation
- **Typical pattern:** A security-critical arithmetic expression (e.g., buffer size computation, privilege calculation, offset computation) produces a wrong result due to operator precedence mistakes, missing terms, or type mixing. This pillar-level CWE is DISCOURAGED for vulnerability mapping — prefer a more specific child like CWE-190, CWE-191, or CWE-131.
- **Checker suggestion:** Insufficient samples, no reliable recommendation yet.

## CWE-834: Excessive Iteration
- **Typical pattern:** A loop bound is derived from untrusted input without an upper cap, allowing an attacker to force the loop to run millions of times (CPU exhaustion) or allocate memory per iteration until the system runs out of resources. Often seen in network protocol parsers that loop while processing packet fragments.
- **Checker suggestion:** Validate that loop-bound inputs are both non-negative and capped at a sane maximum. For loops consuming resources per iteration, insert an explicit iteration counter with a hard upper limit and abort if exceeded.
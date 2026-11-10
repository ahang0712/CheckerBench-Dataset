# CWE-1077: Floating Point Comparison with Incorrect Operator
- **Typical pattern:** Using `==` or `!=` to compare `float`/`double` values directly, ignoring that rounding errors can make mathematically equal numbers differ in representation, causing unexpected comparison results.
- **Checker suggestion:** Flag direct equality/inequality operators applied to floating-point types; recommend using an epsilon-based tolerance comparison (e.g., `fabs(a - b) < EPSILON`).

## CWE-116: Improper Encoding or Escaping of Output
- **Typical pattern:** Constructing structured output (HTML, SQL, shell commands, LDAP queries) by embedding untrusted input without context-appropriate encoding or escaping, allowing injection of control characters.
- **Checker suggestion:** Trace data flow from untrusted sources to output sinks and verify that context-aware encoding (HTML entity, URL, SQL parameterization, etc.) is applied before emission.

## CWE-1188: Initialization of a Resource with an Insecure Default
- **Typical pattern:** Shipping a product with insecure default credentials, world-writable permissions, or open network bindings that administrators are expected to change but often do not.
- **Checker suggestion:** Insufficient samples, no reliable recommendation yet.

## CWE-1321: Improperly Controlled Modification of Object Prototype Attributes ('Prototype Pollution')
- **Typical pattern:** Recursively merging or assigning object attributes from untrusted input (JavaScript) without blocking prototype-accessing keys such as `__proto__`, `constructor`, or `prototype`.
- **Checker suggestion:** Insufficient samples, no reliable recommendation yet. (This weakness is JavaScript-specific and not generally applicable to C/C++ static analysis.)

## CWE-665: Improper Initialization
- **Typical pattern:** Using a variable, pointer, or resource before it has been initialized, leaving it in an undefined or attacker-influenced state; often caused by conditional code paths where one branch skips initialization.
- **Checker suggestion:** Enable compiler warnings for uninitialized variables; use a data-flow analysis that tracks initialization state across all control-flow paths and flags uses of potentially uninitialized resources.

## CWE-668: Exposure of Resource to Wrong Sphere
- **Typical pattern:** A resource (file, socket, memory region) is accessible to a broader set of actors than intended, e.g., world-readable sensitive files, binding to `0.0.0.0` instead of localhost, or missing access-control checks.
- **Checker suggestion:** Verify file permission settings at creation, check network binding addresses, and confirm authorization checks guard all resource-access paths.

## CWE-704: Incorrect Type Conversion or Cast
- **Typical pattern:** Performing a cast between incompatible types (e.g., `int`→`enum`, signed→unsigned of different width, pointer→integer, or union type confusion) that truncates, sign-extends, or reinterprets bits in an unsafe way.
- **Checker suggestion:** Flag narrowing conversions, casts between pointer and non-pointer types, and C-style casts that change both size and signedness; prefer `static_cast`/`reinterpret_cast` with explicit range checks.

## CWE-909: Missing Initialization of Resource
- **Typical pattern:** A critical resource (stack variable, heap buffer, file handle, device register) is never initialized before first use, leading to undefined behavior, information disclosure, or crash.
- **Checker suggestion:** Use compiler warnings (`-Wuninitialized`, `-Wmaybe-uninitialized`) and static analysis that performs path-sensitive initialization checks; enforce initialization at the point of declaration where possible.
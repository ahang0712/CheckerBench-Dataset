# CWE-404: Improper Resource Shutdown or Release
- **Typical pattern:** Resources (file handles, sockets, memory, database connections) are allocated but not properly released on all code paths, especially error/early-return paths. Common in C/C++ code that manually manages resources via fopen/malloc/new without RAII wrappers.
- **Checker suggestion:** Implement a path-sensitive resource-leak checker that tracks allocation at point of creation and verifies release on every exit path, flagging any path where the resource handle goes out of scope without a corresponding close/free/delete.

## CWE-407: Inefficient Algorithmic Complexity
- **Typical pattern:** Attacker-controlled input triggers worst-case algorithmic behavior (e.g., catastrophic regex backtracking, hash-table collisions, O(n²) string parsing). Often seen in regex engines, hash maps with predictable keys, and diff/patch algorithms.
- **Checker suggestion:** Insufficient samples, no reliable recommendation yet.

## CWE-436: Interpretation Conflict
- **Typical pattern:** Two components interpret the same data differently (e.g., proxy vs. server disagreement on Content-Length, firewall vs. OS packet reassembly), enabling smuggling or filter bypass. Most common in HTTP intermediaries, anti-virus, and IDS/IPS products.
- **Checker suggestion:** Insufficient samples, no reliable recommendation yet.

## CWE-440: Expected Behavior Violation
- **Typical pattern:** An API, library function, or hardware register does not behave according to its specification (e.g., strncpy in one OS differs from the standard, or a CSR register accepts writes from an unauthorized source).
- **Checker suggestion:** Insufficient samples, no reliable recommendation yet.

## CWE-455: Non-exit on Failed Initialization
- **Typical pattern:** Product continues execution after a security-critical initialization failure (e.g., invalid ACL config, HSM activation failure, failed privilege separation), operating in a degraded/insecure state without alerting the administrator.
- **Checker suggestion:** Flag initialization sequences where a failure return code is not checked or where the program does not call exit/abort after a fatal init error. Prioritize checks around privilege separation, crypto init, and config parsing.

## CWE-457: Use of Uninitialized Variable
- **Typical pattern:** A local variable is declared but not initialized before being read in a conditional or computation. In C/C++ this produces undefined behavior; in other languages it yields a default value that may be attacker-controlled if the variable is on the stack.
- **Checker suggestion:** Run a front-end data-flow analysis that identifies all variable declarations without initializers and traces every read path to verify a write precedes it. Suppress false positives for variables that are initialized via out-parameters or memset.

## CWE-459: Incomplete Cleanup
- **Typical pattern:** Temporary files, elevated privileges, or modified security attributes are not reverted after use. For example, a setuid binary drops privileges in the main path but leaves them elevated in an error path, or a temp file is left on disk after processing.
- **Checker suggestion:** Track privileged operations (setuid, chroot, temp file creation) and verify a corresponding restore/cleanup call exists on every reachable exit path from the scope where the operation was performed.

## CWE-670: Always-Incorrect Control Flow Implementation
- **Typical pattern:** The code's control flow does not match the intended algorithm — e.g., a loop bound uses `<` when it should use `<=`, or a switch is missing a fall-through `break`, or an if-condition is negated. The logic compiles but produces wrong results.
- **Checker suggestion:** This is difficult to automate in general. Focus on pattern-specific checks: verify that switch cases end with break (unless fall-through is intentional), flag loops where the comparison operator looks mismatched with the initializer, and use assertion-based static checkers to verify programmer-written invariants.

## CWE-672: Operation on a Resource after Expiration or Release
- **Typical pattern:** A pointer or handle is used after it has been freed/closed (use-after-free, dangling pointer). In C/C++ this is undefined behavior; it often leads to exploitable memory corruption.
- **Checker suggestion:** Use a use-after-free checker that tracks allocation/deallocation pairs and reports any dereference of a pointer after the corresponding free/delete. Pair with lifetime analysis for stack variables.

## CWE-680: Integer Overflow to Buffer Overflow
- **Typical pattern:** An integer overflow occurs during a memory-size calculation (e.g., `n * sizeof(T)`), causing less memory to be allocated than needed. The subsequent copy operation then overflows the undersized buffer.
- **Checker suggestion:** Implement a taint-based checker that flags arithmetic on allocation-size expressions where the operands come from untrusted input and the result is passed to malloc/calloc/new. Recommend safe patterns such as calloc or checked arithmetic wrappers.

## CWE-776: Improper Restriction of Recursive Entity References in DTDs (XML Entity Expansion)
- **Typical pattern:** An XML parser with DTD processing enabled is given a document containing deeply nested or mutually recursive entity references, causing exponential expansion (billion laughs attack) and exhausting memory.
- **Checker suggestion:** Verify that the XML parser is configured with DTD processing disabled or that entity expansion limits (e.g., `XMLParser.setFeature("http://apache.org/xml/features/disallow-doctype-decl", true)`) are enforced. This is a configuration check rather than a source-code pattern.

## CWE-833: Deadlock
- **Typical pattern:** Two or more threads acquire locks in different orders, creating a cycle where each thread holds a lock the other needs. Also occurs with nested locks, condition-variable signal ordering, or lock/unlock mismatches.
- **Checker suggestion:** Insufficient samples, no reliable recommendation yet.

## CWE-838: Inappropriate Encoding for Output Context
- **Typical pattern:** Output is encoded with one character encoding (e.g., UTF-8) but the downstream consumer expects another (e.g., ISO-8859-1), leading to mojibake or, in security contexts, XSS or command injection if certain byte sequences are reinterpreted.
- **Checker suggestion:** Insufficient samples, no reliable recommendation yet.
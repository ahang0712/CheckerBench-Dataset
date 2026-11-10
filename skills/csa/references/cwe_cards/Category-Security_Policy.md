# CWE-134: Use of Externally-Controlled Format String
- **Typical pattern:** A format string function (printf, sprintf, syslog) receives a user-controlled string as the format argument rather than as a data argument, e.g., `printf(user_input)` instead of `printf("%s", user_input)`.
- **Checker suggestion:** Flag any call to printf-family or syslog where the format argument is not a string literal or constant. Use taint tracking to detect paths from external input to the format parameter.

## CWE-17: DEPRECATED: Code (Category — PROHIBITED for mapping)
- **Typical pattern:** This is a deprecated category entry originally used to organize the Development View. It should not be used for vulnerability mapping.
- **Checker suggestion:** Do not emit CWE-17; map to a more specific child CWE (e.g., CWE-94, CWE-77, CWE-78) based on the actual weakness.

## CWE-203: Observable Discrepancy
- **Typical pattern:** The application returns different error messages, response times, or status codes depending on whether an operation succeeded or failed (e.g., "invalid user" vs. "invalid password"), enabling side-channel user enumeration or timing attacks.
- **Checker suggestion:** Look for conditional error messages in authentication logic. Recommend uniform responses and constant-time comparisons for secrets.

## CWE-212: Improper Removal of Sensitive Information Before Storage or Transfer
- **Typical pattern:** Sensitive data (passwords, tokens, PII) is logged, cached, or transmitted without being sanitized or redacted first.
- **Checker suggestion:** Insufficient samples, no reliable recommendation yet.

## CWE-22: Improper Limitation of a Pathname to a Restricted Directory ('Path Traversal')
- **Typical pattern:** User-supplied file paths are used in file operations without validating that the resolved path stays within an allowed root directory, enabling `../` traversal.
- **Checker suggestion:** Use taint analysis to track user-controlled strings passed to fopen, open, read, write, or similar file APIs. Flag paths that are not canonicalized or validated against an allowlist.

## CWE-255: Credentials Management Errors (Category — PROHIBITED for mapping)
- **Typical pattern:** A category encompassing weaknesses in credential storage, transmission, and verification. Not to be used directly for mapping.
- **Checker suggestion:** Do not emit CWE-255; map to a child CWE such as CWE-522 (Insufficiently Protected Credentials) or CWE-287 (Improper Authentication).

## CWE-264: Permissions, Privileges, and Access Controls (Category — PROHIBITED for mapping)
- **Typical pattern:** A deprecated category covering permissions, privileges, and access control weaknesses. Not to be used directly for mapping.
- **Checker suggestion:** Do not emit CWE-264; map to a specific child like CWE-284, CWE-862, or CWE-863.

## CWE-269: Improper Privilege Management
- **Typical pattern:** The program does not properly drop, assign, or manage privileges (setuid, capabilities, DAC), allowing a caller to retain elevated permissions longer than necessary.
- **Checker suggestion:** Verify that privilege-escalating calls (setuid, seteuid, cap_set_proc) have corresponding privilege-dropping calls, and check for missing error handling on privilege operations.

## CWE-273: Improper Check for Dropped Privileges
- **Typical pattern:** The code calls setuid() or similar to drop privileges but does not check the return value, so privilege reduction may silently fail.
- **Checker suggestion:** Flag calls to privilege-dropping functions (setuid, setgid, chroot) where the return value is not checked immediately after the call.

## CWE-276: Incorrect Default Permissions
- **Typical pattern:** Files, directories, or shared memory are created with overly permissive default permissions (e.g., umask 0, or using fopen with 0666 without subsequent fchmod).
- **Checker suggestion:** Detect creation of files or IPC objects (shmget, open with O_CREAT) with permissions that grant world-writable or world-readable access when not intended.

## CWE-281: Improper Preservation of Permissions
- **Typical pattern:** During a file copy, move, or privilege-change operation, the original file's permissions are not preserved, potentially exposing sensitive data to unauthorized users.
- **Checker suggestion:** Insufficient samples, no reliable recommendation yet.

## CWE-284: Improper Access Control
- **Typical pattern:** The application fails to enforce access restrictions on resources, allowing unauthorized users to perform privileged operations or access restricted data.
- **Checker suggestion:** Use data-flow analysis to verify access-control checks (authorization gates) precede sensitive operations in all control-flow paths.

## CWE-287: Improper Authentication
- **Typical pattern:** The application does not prove the identity of a user or service before granting access, or uses a weak authentication mechanism that can be bypassed.
- **Checker suggestion:** Identify authentication routines and verify that credentials are validated before sensitive operations. Flag missing or trivially bypassed authentication checks.

## CWE-290: Authentication Bypass by Spoofing
- **Typical pattern:** The application trusts an unverifiable identity assertion (IP address, DNS name, or unauthenticated header) to authenticate a user or service.
- **Checker suggestion:** Insufficient samples, no reliable recommendation yet.

## CWE-295: Improper Certificate Validation
- **Typical pattern:** TLS/SSL certificate validation is disabled, incorrectly configured, or uses a custom verification routine that does not properly check the certificate chain, hostname, or expiration.
- **Checker suggestion:** Flag calls to SSL functions where verification flags disable checks (e.g., `SSL_VERIFY_NONE`, `curl_easy_setopt(CURLOPT_SSL_VERIFYPEER, 0)`).

## CWE-307: Improper Restriction of Excessive Authentication Attempts
- **Typical pattern:** The application does not limit the rate or number of failed login attempts, enabling automated brute-force password guessing.
- **Checker suggestion:** Insufficient samples, no reliable recommendation yet.

## CWE-310: Cryptographic Issues (Category — PROHIBITED for mapping)
- **Typical pattern:** A category encompassing weaknesses in cryptographic implementation and usage. Not to be used directly for vulnerability mapping.
- **Checker suggestion:** Do not emit CWE-310; map to a child CWE such as CWE-327 (Broken/Risky Algorithm), CWE-330 (Insufficiently Random Values), or CWE-338 (Weak PRNG).

## CWE-319: Cleartext Transmission of Sensitive Information
- **Typical pattern:** Sensitive data is transmitted over an unencrypted channel (HTTP, FTP, plain TCP) rather than a protected one (HTTPS, TLS).
- **Checker suggestion:** Flag socket connections or HTTP requests that transmit credentials or sensitive data without TLS. Look for `http://` URLs in authentication contexts.

## CWE-327: Use of a Broken or Risky Cryptographic Algorithm
- **Typical pattern:** The code uses deprecated or weak algorithms such as MD5, SHA1, DES, or ECB-mode AES for security-critical operations.
- **Checker suggestion:** Detect references to weak algorithm constants (e.g., `EVP_md5`, `EVP_des_ecb`) in OpenSSL EVP calls or direct use of weak hash/encryption functions.

## CWE-330: Use of Insufficiently Random Values
- **Typical pattern:** A security-sensitive value (cryptographic key, session ID, nonce, CSRF token) is generated using a predictable random source like `rand()`, `random()`, or a weak seed.
- **Checker suggestion:** Flag uses of `rand()`, `random()`, or `srand()` in security contexts. Recommend using `getrandom()`, `/dev/urandom`, or `RAND_bytes()` instead.

## CWE-338: Use of Cryptographically Weak Pseudo-Random Number Generator (PRNG)
- **Typical pattern:** A PRNG not suited for cryptographic use (e.g., `java.util.Random`, `rand()` in C) is used to generate secrets.
- **Checker suggestion:** Insufficient samples, no reliable recommendation yet.

## CWE-345: Insufficient Verification of Data Authenticity
- **Typical pattern:** The application accepts data without verifying its origin or integrity, such as not checking a MAC, digital signature, or checksum on received data.
- **Checker suggestion:** Identify points where external data is deserialized or processed and verify that authentication/integrity checks (HMAC, signature verification) precede usage.

## CWE-347: Improper Verification of Cryptographic Signature
- **Typical pattern:** The application verifies a digital signature incorrectly — e.g., using the wrong key, not checking the full certificate chain, or ignoring a failed verification result.
- **Checker suggestion:** Insufficient samples, no reliable recommendation yet.

## CWE-354: Improper Validation of Integrity Check Value
- **Typical pattern:** The application either skips integrity checks (CRC, checksum, HMAC) on received data or validates them only after the data is already used.
- **Checker suggestion:** Insufficient samples, no reliable recommendation yet.

## CWE-426: Untrusted Search Path
- **Typical pattern:** The application uses relative paths or relies on the system `PATH`/`LD_LIBRARY_PATH` to load executables or libraries, allowing an attacker to plant a malicious binary higher in the search order.
- **Checker suggestion:** Insufficient samples, no reliable recommendation yet.

## CWE-434: Unrestricted Upload of File with Dangerous Type
- **Typical pattern:** The application allows users to upload files without validating the file type, enabling upload of executable scripts (.php, .jsp, .war) that can be executed on the server.
- **Checker suggestion:** Insufficient samples, no reliable recommendation yet.

## CWE-444: Inconsistent Interpretation of HTTP Requests ('HTTP Request/Response Smuggling')
- **Typical pattern:** Discrepancies between front-end (proxy/load balancer) and back-end server in parsing HTTP headers (Content-Length vs. Transfer-Encoding) enable request smuggling.
- **Checker suggestion:** Flag mixed use of Content-Length and Transfer-Encoding headers in HTTP request handling. Recommend consistent header parsing across proxies and origin servers.

## CWE-522: Insufficiently Protected Credentials
- **Typical pattern:** Passwords, tokens, or API keys are stored in plaintext (in config files, databases, or memory) rather than using hashing or encryption.
- **Checker suggestion:** Insufficient samples, no reliable recommendation yet.

## CWE-532: Insertion of Sensitive Information into Log File
- **Typical pattern:** Logging statements include sensitive data such as passwords, session tokens, or credit card numbers in cleartext.
- **Checker suggestion:** Insufficient samples, no reliable recommendation yet.

## CWE-552: Files or Directories Accessible to External Parties
- **Typical pattern:** Configuration files, backups, or internal resources are placed inside the web root or otherwise exposed to unauthenticated external access.
- **Checker suggestion:** Insufficient samples, no reliable recommendation yet.

## CWE-59: Improper Link Resolution Before File Access ('Link Following')
- **Typical pattern:** The program creates or writes to a temporary file using a predictable name without checking whether it is a symlink, enabling an attacker to redirect the write to an arbitrary file.
- **Checker suggestion:** Flag use of predictable temporary-file names (e.g., `mktemp`, `tmpnam`) in favor of `mkstemp` or `O_CREAT | O_EXCL`. Check that `fstat`/`lstat` is used to verify the file is not a symlink.

## CWE-610: Externally Controlled Reference to a Resource in Another Sphere
- **Typical pattern:** The application accepts a URL or resource identifier from the user and uses it to access a resource outside the intended trust boundary (e.g., SSRF, opening a file from an external host).
- **Checker suggestion:** Insufficient samples, no reliable recommendation yet.

## CWE-613: Insufficient Session Expiration
- **Typical pattern:** Session tokens remain valid after logout, password change, or prolonged inactivity, allowing session reuse by an attacker.
- **Checker suggestion:** Insufficient samples, no reliable recommendation yet.

## CWE-697: Incorrect Comparison
- **Typical pattern:** The code uses `==` for string comparison, uses assignment (`=`) instead of equality (`==`), or uses a weak comparison (`==` or `!=`) for sensitive values where constant-time comparison is needed.
- **Checker suggestion:** Flag non-constant-time string/memory comparisons (`strcmp`, `memcmp`) on security-critical values (passwords, HMACs) and recommend `CRYPTO_memcmp` or similar constant-time variants.

## CWE-707: Improper Neutralization
- **Typical pattern:** The application embeds untrusted input into an output context (HTML, SQL, shell command) without proper escaping or sanitization.
- **Checker suggestion:** This is a broad pillar CWE. Suggest mapping to a specific child (CWE-79, CWE-89, CWE-78, CWE-77) based on the injection context.

## CWE-73: External Control of File Name or Path
- **Typical pattern:** User-controlled input is used directly to construct a file path for open/openat/access without validation, enabling arbitrary file read/write.
- **Checker suggestion:** Insufficient samples, no reliable recommendation yet.

## CWE-732: Incorrect Permission Assignment for Critical Resource
- **Typical pattern:** A critical resource (file, shared memory, device node) is created with permissions that are too permissive, often via `chmod` with overly broad bits or `umask` set too permissively.
- **Checker suggestion:** Flag `chmod`/`fchmod` calls that grant world-writable or world-executable permissions on sensitive files. Recommend `umask 077` or explicit restrictive modes.

## CWE-74: Improper Neutralization of Special Elements in Output Used by a Downstream Component ('Injection')
- **Typical pattern:** Untrusted data is included in an output stream (SQL, shell, LDAP, XML) without escaping special characters, allowing injection attacks.
- **Checker suggestion:** Use taint propagation from input sources to sensitive sinks (exec, system, eval, SQL query builders). Recommend context-aware escaping or parameterized APIs.

## CWE-77: Improper Neutralization of Special Elements used in a Command ('Command Injection')
- **Typical pattern:** User-controlled data flows into a command string passed to `system()`, `popen()`, `execvp()`, or similar, allowing arbitrary shell command execution.
- **Checker suggestion:** Taint-check arguments passed to `system()`, `popen()`, `exec*()`, and `fork()/exec()` combinations. Recommend using `execve` with a fixed argv array rather than shell wrappers.

## CWE-78: Improper Neutralization of Special Elements used in an OS Command ('OS Command Injection')
- **Typical pattern:** Similar to CWE-77 but specifically focused on OS command injection where the user controls arguments that are passed to the OS command interpreter.
- **Checker suggestion:** Taint-track user input flowing into `CreateProcess`, `ShellExecute`, or `Runtime.exec()` in Java, and `system()`/`popen()` in C/C++. Prefer exec-family functions with a hardcoded executable path.

## CWE-79: Improper Neutralization of Input During Web Page Generation ('Cross-site Scripting')
- **Typical pattern:** User-supplied data is reflected in an HTML page or stored and later served to other users without proper HTML entity encoding, enabling script injection.
- **Checker suggestion:** In C/C++ web applications, flag any path where untrusted data is written into an HTTP response body without prior HTML-encoding via an escaping function.

## CWE-862: Missing Authorization
- **Typical pattern:** A function or API endpoint performs a sensitive operation (e.g., read user data, modify settings) without first verifying that the caller has the required permissions.
- **Checker suggestion:** Identify sensitive operations and verify that an authorization check (e.g., a call to an access-control function) is present on every code path leading to that operation.

## CWE-863: Incorrect Authorization
- **Typical pattern:** An authorization check exists but is flawed — the wrong permission is checked, the check can be bypassed due to logic errors, or the enforcement is incomplete.
- **Checker suggestion:** Use path-sensitive analysis to examine authorization predicates for common flaws: off-by-one role IDs, missing else branches, or incorrect operator usage (e.g., `||` instead of `&&`).

## CWE-89: Improper Neutralization of Special Elements used in an SQL Command ('SQL Injection')
- **Typical pattern:** User-controlled input is concatenated into an SQL query string without parameterized statements or proper escaping, allowing database manipulation.
- **Checker suggestion:** Taint-track user input to SQL execution functions (mysql_query, sqlite3_exec, ODBC). Flag any dynamically constructed SQL string; recommend prepared statements with bound parameters.

## CWE-918: Server-Side Request Forgery (SSRF)
- **Typical pattern:** The application accepts a URL from the user and fetches it server-side without validation, allowing requests to internal services (e.g., 127.0.0.1, AWS metadata endpoints).
- **Checker suggestion:** Insufficient samples, no reliable recommendation yet.

## CWE-94: Improper Control of Generation of Code ('Code Injection')
- **Typical pattern:** User input is passed to a dynamic code evaluation function (`eval`, `dlopen` with constructed paths, `LoadLibrary` with user-controlled name) enabling arbitrary code execution.
- **Checker suggestion:** Insufficient samples, no reliable recommendation yet.
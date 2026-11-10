# CWE-252: Unchecked Return Value
- **Typical pattern:** A function that returns a status code, error indicator, or pointer is called and its return value is discarded without inspection. Common examples include ignoring the return of malloc(), setuid(), fgets(), pthread_mutex_lock(), or gethostbyaddr(), leading to use of invalid state or undefined behavior.
- **Checker suggestion:** Use a checker that tracks "must-check" return values — flag any call to a known security-sensitive function where the return value is not consumed in a conditional or compared against an error sentinel.

## CWE-388: CWE CATEGORY: 7PK - Errors
- **Typical pattern:** This is an organizational category (PROHIBITED for mapping) grouping weaknesses around improper error handling, including unchecked conditions and uncaught exceptions. It is not a weakness itself.
- **Checker suggestion:** Insufficient samples, no reliable recommendation yet.

## CWE-417: CWE CATEGORY: Communication Channel Errors
- **Typical pattern:** This is an organizational category (PROHIBITED for mapping) grouping weaknesses related to improper handling of communication channels and access paths, such as unprotected channels, forced browsing, and origin validation errors. It is not a weakness itself.
- **Checker suggestion:** Insufficient samples, no reliable recommendation yet.

## CWE-754: Improper Check for Unusual or Exceptional Conditions
- **Typical pattern:** The programmer assumes an unusual condition (e.g., low memory, I/O failure, DNS resolution failure) will never occur and omits any check before using the result. Attackers deliberately trigger these conditions to violate assumptions and induce instability or information disclosure.
- **Checker suggestion:** Use a checker that identifies missing validation after calls to system/library APIs documented to return error indicators, especially when the result flows into a pointer dereference, array index, or privilege-sensitive operation.

## CWE-755: Improper Handling of Exceptional Conditions
- **Typical pattern:** An error or exception is detected (e.g., NULL-check after malloc, catching an exception) but the handler is empty, logs without recovering, or fails to clean up resources, leaving the system in an inconsistent or insecure state.
- **Checker suggestion:** Use a checker that flags empty catch blocks, catch-all exception handlers that perform no recovery action, and error-handling paths that omit necessary cleanup before propagating or returning.
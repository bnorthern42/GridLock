## 2025-01-20 - QRegularExpression performance
**Learning:** Instantiating `QRegularExpression` inside loops or frequently called functions (like message parsing handlers) is a common performance bottleneck in Qt. Compiling a regular expression is relatively expensive.
**Action:** Move frequently used `QRegularExpression` instances to static variables, member variables, or class-level constants.
## Security Patch: GDB MI Command Injection Fixed
**Action**: Fixed command injection vulnerability in GDB MI string evaluation in `GdbRankCoordinator.cpp`.
**Learning**: When supplying strings or arbitrary expressions to GDB MI interface functions like `-data-evaluate-expression`, always securely escape characters as proper C-strings. Specifically, newline characters (`\n`, `\r`) must be escaped; otherwise, GDB will interpret them as command separators, allowing arbitrary command execution. Replaced simple, flawed string wrapping with a full character-by-character escape helper function.

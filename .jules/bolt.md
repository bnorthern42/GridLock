## 2025-01-20 - QRegularExpression performance
**Learning:** Instantiating `QRegularExpression` inside loops or frequently called functions (like message parsing handlers) is a common performance bottleneck in Qt. Compiling a regular expression is relatively expensive.
**Action:** Move frequently used `QRegularExpression` instances to static variables, member variables, or class-level constants.

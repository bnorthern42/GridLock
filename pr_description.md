🎯 **What:** The vulnerability fixed
The `GdbRankCoordinator::evaluateExpression` and `evaluateHoverVariable` functions were vulnerable to GDB MI command injection. Because the user-provided expression string was only wrapped in quotes (and selectively escaped only if it didn't already start/end with quotes), a payload like `"\n-exec-run\n"` would pass through unescaped. The newline characters allowed an attacker (or a malicious application) to break out of the intended `-data-evaluate-expression` command and execute arbitrary GDB commands such as `-exec-run` or memory modification operations.

⚠️ **Risk:** The potential impact if left unfixed
An attacker could gain arbitrary command execution within the debugging session. This could be leveraged to bypass intended restrictions, manipulate memory, or execute underlying host commands via GDB's capabilities, compromising the integrity of the debugging environment and potentially the host machine.

🛡️ **Solution:** How the fix addresses the vulnerability
Implemented a robust helper function, `escapeGdbMiString`, which processes the input string character-by-character. It properly escapes all sensitive characters (backslashes, double quotes, and critical control characters like newlines, carriage returns, and tabs) into their C-string literal equivalents (e.g., `\n` to `\\n`). This ensures the entire expression is treated strictly as a single string literal argument by GDB MI, effectively neutralizing any command injection vectors.

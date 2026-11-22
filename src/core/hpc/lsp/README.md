# LSP Hover Coordinator (`src/core/hpc/lsp/`)

Lightweight JSON-RPC 2.0 `clangd` client dedicated to asynchronous AST symbol type and docstring hover resolution in source views.

## Contents

- `LspCoordinator.hpp` (`102` lines):
  - Declares `LspCoordinator` (`QObject`), encapsulating a child `clangd` `QProcess`.
  - Manages stdio `Content-Length` framing buffers and JSON-RPC request sequence IDs.
  - Exposes methods to launch/stop `clangd`, sync open document buffers (`didOpen` / `didChange`), and issue `textDocument/hover` queries at `(file, line, column)`.
  - Emits normalized hover markdown signals back to source editor views.
- `LspCoordinator.cpp`:
  - Implements child `clangd` process startup, stdio pipe reading, and shutdown teardown.
  - Performs exact `Content-Length` framing and fragmented JSON packet reassembly.
  - Matches response IDs back to hover coordinates and extracts `MarkupContent` (`markdown` / `plaintext`).
  - Handles missing `clangd` executables, truncated headers, and malformed JSON objects safely.

## Expected Behavior & Invariants

- **Asynchronous Non-Blocking Queries**:
  - All `clangd` IPC runs via `QProcess` signals; slow, crashed, or missing `clangd` installs never block editor scrolling or typing.
- **Fragmented Message Reassembly**:
  - Partial `Content-Length` headers and multi-chunk JSON responses buffer across reads until exact byte counts arrive (`tests/hpc/test_lsp.cpp`).
- **Large-File & Plain-Data Bypass**:
  - `SourceCodeView` and `EditorTabManager` skip `clangd` document sync for `.csv`/`.tsv` files and files `>= 512 KB` (`524,288` bytes).
- **Scoped LSP Responsibility Split**:
  - `LspCoordinator` handles editor hover telemetry, whereas conditional breakpoint virtual-line splicing and completion live in `src/core/utils/ClangdLspClient.{hpp,cpp}`.
- **Clean Child Teardown**:
  - Destroying `LspCoordinator` closes stdio pipes and terminates active `clangd` child processes cleanly.
- **Verification**:
  - Run `meson test -C builddir lsp_tests --print-errorlogs`.

## Maintainer & Agent Notes

- Verify all changes under `QT_QPA_PLATFORM=offscreen` where live system `clangd` binaries may be absent or stubbed cleanly.
- Keep `lsp_tests` dependencies in `meson.build` minimal (`LspCoordinator.cpp` + `QtCore`), avoiding any `QtWidgets` or `MainWindow` includes.
- If adding new LSP methods beyond hover (e.g., diagnostics or definitions), align shared JSON-RPC framing conventions cleanly with `src/core/utils/ClangdLspClient`.
- Never re-enable `clangd` sync for `.csv`/`.tsv` buffers or large files `>= 512 KB`, because background AST parsing stalls UI responsiveness.
- Normalize local file paths with `QUrl::fromLocalFile(...)` before encoding `textDocument.uri` parameters in JSON-RPC requests.
- Document buffer boundary edge cases with inline comments, keep files strictly under `< 950` lines, and update this `README.md` when adding helpers.

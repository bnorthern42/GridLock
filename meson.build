project('GridLock', 'cpp', 'c',
  version : '0.5.2',
  default_options : ['warning_level=3', 'cpp_std=c++23'],
  subproject_dir : 'external')

add_project_arguments('-Wall', '-Wextra', '-Wpedantic', '-Wunused-variable', '-Wunused-parameter', language: 'cpp')

# Dependencies
qt6 = import('qt6')
qt6_dep = dependency('qt6', modules: ['Core', 'Widgets', 'Gui', 'Test', 'Sql', 'Network', 'OpenGLWidgets', 'Concurrent'])
tomlplusplus_dep = dependency('tomlplusplus', fallback: ['tomlplusplus', 'tomlplusplus_dep'])


# Source files
src_files = [
  'src/main.cpp',
  'src/GridLockApp.cpp',
  'src/core/hpc/NativeMemoryReader.cpp',
  'src/core/hpc/MemoryBoundsValidator.cpp',
  'src/core/hpc/GdbRankCoordinator.cpp',
  'src/core/hpc/DapCoordinator.cpp',
  'src/core/hpc/MiSanitizer.cpp',
  'src/core/hpc/TelemetryObfuscator.cpp',
  'src/core/hpc/MemoryExporter.cpp',
  'src/core/hpc/MemoryDiffer.cpp',
  'src/core/managers/ConfigManager.cpp',
  'src/core/managers/SessionManager.cpp',
  'src/core/hpc/LspCoordinator.cpp',
  'src/core/hpc/DeadlockAnalyzer.cpp',
  'src/ui/MainWindow.cpp',
  'src/ui/views/ServerRackView.cpp',
  'src/ui/views/SourceCodeView.cpp',
  'src/ui/dialogs/ConditionalBreakpointDialog.cpp',
  'src/ui/EditorTabManager.cpp',
  'src/ui/widgets/ProjectExplorerWidget.cpp',
  'src/ui/dialogs/ProjectSettingsDialog.cpp',
  'src/ui/dialogs/ProjectWizard.cpp',
  'src/ui/widgets/ExpressionEvaluatorWidget.cpp',
  'src/ui/views/DisassemblyView.cpp',
  'src/ui/widgets/ReferenceManualWidget.cpp',
  'src/ui/widgets/DifferentialGrid.cpp',
  'src/ui/docks/TerminalDock.cpp',
  'src/ui/widgets/MpiDiagnosticsWidget.cpp',
  'src/ui/GdbConsoleWidget.cpp',
  'src/backend/TestGenerator.cpp',
  'src/backend/GridLockAutomationRunner.cpp',
  'src/ui/views/MemView.cpp',
  'src/ui/views/RegisterView.cpp',
  'src/ui/dialogs/PreferencesDialog.cpp',
  'src/core/hpc/MockHpcBackend.cpp',
  'src/core/managers/SpackManager.cpp',
  'src/core/commands/DebugCommands.cpp',
  'src/core/managers/ShortcutManager.cpp',
  'src/core/managers/DocsetManager.cpp',
  'src/core/managers/ThemeManager.cpp',
  'src/core/models/VariableTreeModel.cpp',
  'src/ui/docks/VariablesDockWidget.cpp',
  'src/core/utils/CliParser.cpp',
  'src/ui/tutorial/TutorialDialog.cpp',
]

# Headers for MOC (Meta-Object Compiler)
moc_headers = [
  'src/GridLockApp.hpp',
  'src/core/hpc/GdbRankCoordinator.hpp',
  'src/core/hpc/DapCoordinator.hpp',
  'src/core/hpc/TelemetryObfuscator.hpp',
  'src/core/hpc/DeadlockAnalyzer.hpp',
  'src/core/hpc/LspCoordinator.hpp',
  'src/ui/MainWindow.hpp',
  'src/ui/views/ServerRackView.hpp',
  'src/ui/views/SourceCodeView.hpp',
  'src/ui/dialogs/ConditionalBreakpointDialog.hpp',
  'src/ui/EditorTabManager.hpp',
  'src/ui/widgets/ProjectExplorerWidget.hpp',
  'src/ui/dialogs/ProjectSettingsDialog.hpp',
  'src/ui/dialogs/ProjectWizard.hpp',
  'src/ui/widgets/ExpressionEvaluatorWidget.hpp',
  'src/ui/views/DisassemblyView.hpp',
  'src/ui/widgets/ReferenceManualWidget.hpp',
  'src/ui/widgets/DifferentialGrid.hpp',
  'src/ui/docks/TerminalDock.hpp',
  'src/ui/widgets/MpiDiagnosticsWidget.hpp',
  'src/ui/GdbConsoleWidget.hpp',
  'src/core/hpc/IBackendCoordinator.hpp',
  'src/backend/GridLockAutomationRunner.hpp',
  'src/ui/views/MemView.hpp',
  'src/ui/views/RegisterView.hpp',
  'src/ui/dialogs/PreferencesDialog.hpp',
  'src/core/hpc/HpcBackend.hpp',
  'src/core/hpc/MockHpcBackend.hpp',
  'src/core/managers/SpackManager.hpp',
  'src/core/managers/ShortcutManager.hpp',
  'src/core/managers/DocsetManager.hpp',
  'src/core/managers/ThemeManager.hpp',
  'src/core/managers/SessionManager.hpp',
  'src/core/models/VariableTreeModel.hpp',
  'src/ui/docks/VariablesDockWidget.hpp',
  'src/ui/tutorial/TutorialDialog.hpp',
]

# Process Qt6 files
processed_qt_files = qt6.compile_moc(
  headers : moc_headers,
  dependencies: qt6_dep
)

compiled_resources = qt6.compile_resources(
  name: 'gridlock_resources',
  sources: 'resources/gridlock.qrc'
)

# Include directories
inc = include_directories('src')

# Executable
executable('gridlock',
  src_files,
  processed_qt_files,
  compiled_resources,
  include_directories : inc,
  dependencies : [qt6_dep, tomlplusplus_dep],
  install : true)

install_data('data/gridlock.desktop', 
             install_dir: get_option('datadir') / 'applications')

install_data('resources/icon.png', 
             rename: 'gridlock.png',
             install_dir: get_option('datadir') / 'icons/hicolor/512x512/apps')

# Test executable
test_src = [
  'tests/test_coordinator.cpp',
  'src/core/hpc/GdbRankCoordinator.cpp',
  'src/core/managers/ConfigManager.cpp'
]

test_moc_headers = [
  'tests/test_coordinator.hpp',
  'src/core/hpc/GdbRankCoordinator.hpp',
  'src/core/hpc/IBackendCoordinator.hpp'
]

processed_test_files = qt6.compile_moc(
  headers : test_moc_headers,
  dependencies: qt6_dep
)

gridlock_tests = executable('gridlock_tests',
  test_src,
  processed_test_files,
  include_directories : inc,
  dependencies : [qt6_dep, tomlplusplus_dep]
)

test('coordinator_tests', gridlock_tests)

lsp_test_src = [
  'tests/test_lsp.cpp',
  'src/core/hpc/LspCoordinator.cpp',
  'src/core/managers/ConfigManager.cpp'
]

lsp_test_moc_headers = [
  'tests/test_lsp.hpp',
  'src/core/hpc/LspCoordinator.hpp'
]

processed_lsp_test_files = qt6.compile_moc(
  headers : lsp_test_moc_headers,
  dependencies: qt6_dep
)

gridlock_lsp_tests = executable('gridlock_lsp_tests',
  lsp_test_src,
  processed_lsp_test_files,
  include_directories : inc,
  dependencies : [qt6_dep, tomlplusplus_dep]
)

test('lsp_tests', gridlock_lsp_tests)

ui_test_src = [
  'tests/test_ui.cpp',
  'src/ui/MainWindow.cpp',
  'src/ui/views/SourceCodeView.cpp',
  'src/ui/dialogs/ConditionalBreakpointDialog.cpp',
  'src/ui/EditorTabManager.cpp',
  'src/ui/widgets/ProjectExplorerWidget.cpp',
  'src/ui/dialogs/ProjectSettingsDialog.cpp',
  'src/ui/dialogs/ProjectWizard.cpp',
  'src/ui/tutorial/TutorialDialog.cpp',
  'src/ui/widgets/ExpressionEvaluatorWidget.cpp',
  'src/ui/views/ServerRackView.cpp',
  'src/ui/views/DisassemblyView.cpp',
  'src/ui/widgets/ReferenceManualWidget.cpp',
  'src/ui/widgets/DifferentialGrid.cpp',
  'src/ui/docks/TerminalDock.cpp',
  'src/ui/widgets/MpiDiagnosticsWidget.cpp',
  'src/ui/GdbConsoleWidget.cpp',
  'src/core/hpc/GdbRankCoordinator.cpp',
  'src/core/hpc/DeadlockAnalyzer.cpp',
  'src/core/hpc/DapCoordinator.cpp',
  'src/core/hpc/NativeMemoryReader.cpp',
  'src/core/hpc/MemoryExporter.cpp',
  'src/core/hpc/MemoryDiffer.cpp',
  'src/core/managers/ConfigManager.cpp',
  'src/core/managers/SessionManager.cpp',
  'src/core/hpc/LspCoordinator.cpp',
  'src/ui/views/MemView.cpp',
  'src/ui/views/RegisterView.cpp',
  'src/ui/dialogs/PreferencesDialog.cpp',
  'src/core/hpc/MockHpcBackend.cpp',
  'src/core/managers/SpackManager.cpp',
  'src/core/commands/DebugCommands.cpp',
  'src/core/managers/ShortcutManager.cpp',
  'src/core/managers/DocsetManager.cpp',
  'src/core/managers/ThemeManager.cpp',
  'src/core/models/VariableTreeModel.cpp',
  'src/ui/docks/VariablesDockWidget.cpp',
]

ui_test_moc_headers = [
  'tests/test_ui.hpp',
  'src/ui/MainWindow.hpp',
  'src/ui/views/SourceCodeView.hpp',
  'src/ui/dialogs/ConditionalBreakpointDialog.hpp',
  'src/ui/EditorTabManager.hpp',
  'src/ui/widgets/ProjectExplorerWidget.hpp',
  'src/ui/dialogs/ProjectSettingsDialog.hpp',
  'src/ui/dialogs/ProjectWizard.hpp',
  'src/ui/tutorial/TutorialDialog.hpp',
  'src/ui/widgets/ExpressionEvaluatorWidget.hpp',
  'src/ui/views/ServerRackView.hpp',
  'src/ui/views/DisassemblyView.hpp',
  'src/ui/widgets/ReferenceManualWidget.hpp',
  'src/ui/widgets/DifferentialGrid.hpp',
  'src/ui/docks/TerminalDock.hpp',
  'src/ui/widgets/MpiDiagnosticsWidget.hpp',
  'src/ui/GdbConsoleWidget.hpp',
  'src/core/hpc/GdbRankCoordinator.hpp',
  'src/core/hpc/DeadlockAnalyzer.hpp',
  'src/core/hpc/DapCoordinator.hpp',
  'src/core/hpc/IBackendCoordinator.hpp',
  'src/core/hpc/LspCoordinator.hpp',
  'src/ui/views/MemView.hpp',
  'src/ui/views/RegisterView.hpp',
  'src/ui/dialogs/PreferencesDialog.hpp',
  'src/core/hpc/HpcBackend.hpp',
  'src/core/hpc/MockHpcBackend.hpp',
  'src/core/managers/SpackManager.hpp',
  'src/core/managers/ShortcutManager.hpp',
  'src/core/managers/DocsetManager.hpp',
  'src/core/managers/ThemeManager.hpp',
  'src/core/managers/SessionManager.hpp',
  'src/core/models/VariableTreeModel.hpp',
  'src/ui/docks/VariablesDockWidget.hpp',
]

processed_ui_test_files = qt6.compile_moc(
  headers : ui_test_moc_headers,
  dependencies: qt6_dep
)

gridlock_ui_tests = executable('gridlock_ui_tests',
  ui_test_src,
  processed_ui_test_files,
  include_directories : inc,
  dependencies : [qt6_dep, tomlplusplus_dep]
)

test('ui_tests', gridlock_ui_tests)

config_test_src = [
  'tests/test_config.cpp',
  'src/core/managers/ConfigManager.cpp'
]

config_test_moc_headers = [
  'tests/test_config.hpp'
]

processed_config_test_files = qt6.compile_moc(
  headers : config_test_moc_headers,
  dependencies: qt6_dep
)

gridlock_config_tests = executable('gridlock_config_tests',
  config_test_src,
  processed_config_test_files,
  include_directories : inc,
  dependencies : [qt6_dep, tomlplusplus_dep]
)

test('config_tests', gridlock_config_tests)

config_engine_test_src = [
  'tests/test_config_engine.cpp',
  'src/core/managers/ConfigManager.cpp',
  'src/core/managers/SessionManager.cpp'
]

config_engine_test_moc_headers = [
  'tests/test_config_engine.hpp',
  'src/core/managers/SessionManager.hpp'
]

processed_config_engine_test_files = qt6.compile_moc(
  headers : config_engine_test_moc_headers,
  dependencies: qt6_dep
)

gridlock_config_engine_tests = executable('gridlock_config_engine_tests',
  config_engine_test_src,
  processed_config_engine_test_files,
  include_directories : inc,
  dependencies : [qt6_dep, tomlplusplus_dep]
)

test('config_engine_tests', gridlock_config_engine_tests)

docset_test_src = [
  'tests/test_docset.cpp',
  'src/core/managers/DocsetManager.cpp',
  'src/core/managers/ConfigManager.cpp'
]

docset_test_moc_headers = [
  'tests/test_docset.hpp',
  'src/core/managers/DocsetManager.hpp'
]

processed_docset_test_files = qt6.compile_moc(
  headers : docset_test_moc_headers,
  dependencies: qt6_dep
)

gridlock_docset_tests = executable('gridlock_docset_tests',
  docset_test_src,
  processed_docset_test_files,
  include_directories : inc,
  dependencies : [qt6_dep, tomlplusplus_dep]
)

test('docset_tests', gridlock_docset_tests)

commands_test_src = [
  'tests/test_commands.cpp',
  'src/core/commands/DebugCommands.cpp',
  'src/core/hpc/GdbRankCoordinator.cpp',
  'src/core/hpc/DapCoordinator.cpp',
  'src/core/hpc/NativeMemoryReader.cpp',
  'src/core/managers/ConfigManager.cpp',
  'src/ui/views/SourceCodeView.cpp',
  'src/ui/dialogs/ConditionalBreakpointDialog.cpp'
]

commands_test_moc_headers = [
  'tests/test_commands.hpp',
  'src/core/hpc/GdbRankCoordinator.hpp',
  'src/core/hpc/DapCoordinator.hpp',
  'src/core/hpc/IBackendCoordinator.hpp',
  'src/ui/views/SourceCodeView.hpp',
  'src/ui/dialogs/ConditionalBreakpointDialog.hpp'
]

processed_commands_test_files = qt6.compile_moc(
  headers : commands_test_moc_headers,
  dependencies: qt6_dep
)

gridlock_commands_tests = executable('gridlock_commands_tests',
  commands_test_src,
  processed_commands_test_files,
  include_directories : inc,
  dependencies : [qt6_dep, tomlplusplus_dep]
)

test('commands_tests', gridlock_commands_tests)
  
dap_parser_test_src = [
  'tests/test_dap_parser.cpp',
  'src/core/hpc/DapCoordinator.cpp',
  'src/core/hpc/NativeMemoryReader.cpp',
  'src/core/managers/ConfigManager.cpp'
]

dap_parser_test_moc_headers = [
  'tests/test_dap_parser.hpp',
  'src/core/hpc/DapCoordinator.hpp',
  'src/core/hpc/IBackendCoordinator.hpp'
]

processed_dap_parser_test_files = qt6.compile_moc(
  headers : dap_parser_test_moc_headers,
  dependencies: qt6_dep
)

gridlock_dap_parser_tests = executable('gridlock_dap_parser_tests',
  dap_parser_test_src,
  processed_dap_parser_test_files,
  include_directories : inc,
  dependencies : [qt6_dep, tomlplusplus_dep]
)

test('dap_parser_tests', gridlock_dap_parser_tests)

dap_commands_test_src = [
  'tests/test_dap_commands.cpp',
  'src/core/hpc/DapCoordinator.cpp',
  'src/core/hpc/NativeMemoryReader.cpp',
  'src/core/hpc/GdbRankCoordinator.cpp',
  'src/core/commands/DebugCommands.cpp',
  'src/core/managers/ConfigManager.cpp',
  'src/ui/views/SourceCodeView.cpp',
  'src/ui/dialogs/ConditionalBreakpointDialog.cpp'
]

dap_commands_test_moc_headers = [
  'tests/test_dap_commands.hpp',
  'src/core/hpc/DapCoordinator.hpp',
  'src/core/hpc/GdbRankCoordinator.hpp',
  'src/core/hpc/IBackendCoordinator.hpp',
  'src/ui/views/SourceCodeView.hpp',
  'src/ui/dialogs/ConditionalBreakpointDialog.hpp'
]

processed_dap_commands_test_files = qt6.compile_moc(
  headers : dap_commands_test_moc_headers,
  dependencies: qt6_dep
)

gridlock_dap_commands_tests = executable('gridlock_dap_commands_tests',
  dap_commands_test_src,
  processed_dap_commands_test_files,
  include_directories : inc,
  dependencies : [qt6_dep, tomlplusplus_dep]
)

test('dap_commands_tests', gridlock_dap_commands_tests)

dap_breakpoints_test_src = [
  'tests/test_dap_breakpoints.cpp',
  'src/core/hpc/DapCoordinator.cpp',
  'src/core/hpc/NativeMemoryReader.cpp',
  'src/core/managers/ConfigManager.cpp'
]

dap_breakpoints_test_moc_headers = [
  'tests/test_dap_breakpoints.hpp',
  'src/core/hpc/DapCoordinator.hpp',
  'src/core/hpc/IBackendCoordinator.hpp'
]

processed_dap_breakpoints_test_files = qt6.compile_moc(
  headers : dap_breakpoints_test_moc_headers,
  dependencies: qt6_dep
)

gridlock_dap_breakpoints_tests = executable('gridlock_dap_breakpoints_tests',
  dap_breakpoints_test_src,
  processed_dap_breakpoints_test_files,
  include_directories : inc,
  dependencies : [qt6_dep, tomlplusplus_dep]
)

test('dap_breakpoints_tests', gridlock_dap_breakpoints_tests)

dap_stacktrace_test_src = [
  'tests/test_dap_stacktrace.cpp',
  'src/core/hpc/DapCoordinator.cpp',
  'src/core/hpc/NativeMemoryReader.cpp',
  'src/core/managers/ConfigManager.cpp'
]

dap_stacktrace_test_moc_headers = [
  'tests/test_dap_stacktrace.hpp',
  'src/core/hpc/DapCoordinator.hpp',
  'src/core/hpc/IBackendCoordinator.hpp'
]

processed_dap_stacktrace_test_files = qt6.compile_moc(
  headers : dap_stacktrace_test_moc_headers,
  dependencies: qt6_dep
)

gridlock_dap_stacktrace_tests = executable('gridlock_dap_stacktrace_tests',
  dap_stacktrace_test_src,
  processed_dap_stacktrace_test_files,
  include_directories : inc,
  dependencies : [qt6_dep, tomlplusplus_dep]
)

test('dap_stacktrace_tests', gridlock_dap_stacktrace_tests)

dap_variables_test_src = [
  'tests/test_dap_variables.cpp',
  'src/core/hpc/DapCoordinator.cpp',
  'src/core/hpc/NativeMemoryReader.cpp',
  'src/core/hpc/GdbRankCoordinator.cpp',
  'src/core/managers/ConfigManager.cpp',
  'src/core/models/VariableTreeModel.cpp'
]

dap_variables_test_moc_headers = [
  'tests/test_dap_variables.hpp',
  'src/core/hpc/DapCoordinator.hpp',
  'src/core/hpc/GdbRankCoordinator.hpp',
  'src/core/hpc/IBackendCoordinator.hpp',
  'src/core/models/VariableTreeModel.hpp'
]

processed_dap_variables_test_files = qt6.compile_moc(
  headers : dap_variables_test_moc_headers,
  dependencies: qt6_dep
)

gridlock_dap_variables_tests = executable('gridlock_dap_variables_tests',
  dap_variables_test_src,
  processed_dap_variables_test_files,
  include_directories : inc,
  dependencies : [qt6_dep, tomlplusplus_dep]
)

test('dap_variables_tests', gridlock_dap_variables_tests)

dap_evaluate_test_src = [
  'tests/test_dap_evaluate.cpp',
  'src/core/hpc/DapCoordinator.cpp',
  'src/core/hpc/NativeMemoryReader.cpp',
  'src/core/managers/ConfigManager.cpp'
]

dap_evaluate_test_moc_headers = [
  'tests/test_dap_evaluate.hpp',
  'src/core/hpc/DapCoordinator.hpp',
  'src/core/hpc/IBackendCoordinator.hpp'
]

processed_dap_evaluate_test_files = qt6.compile_moc(
  headers : dap_evaluate_test_moc_headers,
  dependencies: qt6_dep
)

gridlock_dap_evaluate_tests = executable('gridlock_dap_evaluate_tests',
  dap_evaluate_test_src,
  processed_dap_evaluate_test_files,
  include_directories : inc,
  dependencies : [qt6_dep, tomlplusplus_dep]
)

test('dap_evaluate_tests', gridlock_dap_evaluate_tests)

dap_lifecycle_test_src = [
  'tests/test_dap_lifecycle.cpp',
  'src/core/hpc/DapCoordinator.cpp',
  'src/core/hpc/NativeMemoryReader.cpp',
  'src/core/managers/ConfigManager.cpp'
]

dap_lifecycle_test_moc_headers = [
  'tests/test_dap_lifecycle.hpp',
  'src/core/hpc/DapCoordinator.hpp',
  'src/core/hpc/IBackendCoordinator.hpp'
]

processed_dap_lifecycle_test_files = qt6.compile_moc(
  headers : dap_lifecycle_test_moc_headers,
  dependencies: qt6_dep
)

gridlock_dap_lifecycle_tests = executable('gridlock_dap_lifecycle_tests',
  dap_lifecycle_test_src,
  processed_dap_lifecycle_test_files,
  include_directories : inc,
  dependencies : [qt6_dep, tomlplusplus_dep]
)

test('dap_lifecycle_tests', gridlock_dap_lifecycle_tests)

dap_memory_test_src = [
  'tests/test_dap_memory.cpp',
  'src/core/hpc/DapCoordinator.cpp',
  'src/core/hpc/NativeMemoryReader.cpp',
  'src/core/managers/ConfigManager.cpp'
]

dap_memory_test_moc_headers = [
  'tests/test_dap_memory.hpp',
  'src/core/hpc/DapCoordinator.hpp',
  'src/core/hpc/IBackendCoordinator.hpp'
]

processed_dap_memory_test_files = qt6.compile_moc(
  headers : dap_memory_test_moc_headers,
  dependencies: qt6_dep
)

gridlock_dap_memory_tests = executable('gridlock_dap_memory_tests',
  dap_memory_test_src,
  processed_dap_memory_test_files,
  include_directories : inc,
  dependencies : [qt6_dep, tomlplusplus_dep]
)

test('dap_memory_tests', gridlock_dap_memory_tests)

hpc_memory_test_src = [
  'tests/test_hpc_memory.cpp',
  'src/core/hpc/NativeMemoryReader.cpp'
]

hpc_memory_test_moc_headers = [
  'tests/test_hpc_memory.hpp'
]

processed_hpc_memory_test_files = qt6.compile_moc(
  headers : hpc_memory_test_moc_headers,
  dependencies: qt6_dep
)

gridlock_hpc_memory_tests = executable('gridlock_hpc_memory_tests',
  hpc_memory_test_src,
  processed_hpc_memory_test_files,
  include_directories : inc,
  dependencies : [qt6_dep, tomlplusplus_dep]
)

test('hpc_memory_tests', gridlock_hpc_memory_tests)

stride_security_test_src = [
  'tests/test_stride_security.cpp',
  'src/core/hpc/MemoryBoundsValidator.cpp'
]

stride_security_test_moc_headers = [
  'tests/test_stride_security.hpp'
]

processed_stride_security_test_files = qt6.compile_moc(
  headers : stride_security_test_moc_headers,
  dependencies: qt6_dep
)

gridlock_stride_security_tests = executable('gridlock_stride_security_tests',
  stride_security_test_src,
  processed_stride_security_test_files,
  include_directories : inc,
  dependencies : [qt6_dep, tomlplusplus_dep]
)

test('stride_security_tests', gridlock_stride_security_tests)

dap_native_bridge_test_src = [
  'tests/test_dap_native_bridge.cpp',
  'src/core/hpc/DapCoordinator.cpp',
  'src/core/hpc/NativeMemoryReader.cpp',
  'src/core/managers/ConfigManager.cpp'
]

dap_native_bridge_test_moc_headers = [
  'tests/test_dap_native_bridge.hpp',
  'src/core/hpc/DapCoordinator.hpp',
  'src/core/hpc/IBackendCoordinator.hpp'
]

processed_dap_native_bridge_test_files = qt6.compile_moc(
  headers : dap_native_bridge_test_moc_headers,
  dependencies: qt6_dep
)

gridlock_dap_native_bridge_tests = executable('gridlock_dap_native_bridge_tests',
  dap_native_bridge_test_src,
  processed_dap_native_bridge_test_files,
  include_directories : inc,
  dependencies : [qt6_dep, tomlplusplus_dep]
)

test('dap_native_bridge_tests', gridlock_dap_native_bridge_tests)

sanitizer_test_src = [
  'tests/test_sanitizer.cpp',
  'src/core/hpc/MiSanitizer.cpp'
]

sanitizer_test_moc_headers = [
  'tests/test_sanitizer.hpp'
]

processed_sanitizer_test_files = qt6.compile_moc(
  headers : sanitizer_test_moc_headers,
  dependencies: qt6_dep
)

gridlock_sanitizer_tests = executable('gridlock_sanitizer_tests',
  sanitizer_test_src,
  processed_sanitizer_test_files,
  include_directories : inc,
  dependencies : [qt6_dep, tomlplusplus_dep]
)

test('sanitizer_tests', gridlock_sanitizer_tests)

obfuscator_test_src = [
  'tests/test_obfuscator.cpp',
  'src/core/hpc/TelemetryObfuscator.cpp'
]

obfuscator_test_moc_headers = [
  'tests/test_obfuscator.hpp',
  'src/core/hpc/TelemetryObfuscator.hpp'
]

processed_obfuscator_test_files = qt6.compile_moc(
  headers : obfuscator_test_moc_headers,
  dependencies: qt6_dep
)

gridlock_obfuscator_tests = executable('gridlock_obfuscator_tests',
  obfuscator_test_src,
  processed_obfuscator_test_files,
  include_directories : inc,
  dependencies : [qt6_dep, tomlplusplus_dep]
)

test('obfuscator_tests', gridlock_obfuscator_tests)

advanced_features_test_src = [
  'tests/test_advanced_features.cpp',
  'src/core/hpc/DeadlockAnalyzer.cpp',
  'src/core/hpc/GdbRankCoordinator.cpp',
  'src/core/models/VariableTreeModel.cpp',
  'src/core/managers/ConfigManager.cpp'
]

advanced_features_test_moc_headers = [
  'tests/test_advanced_features.hpp',
  'src/core/hpc/DeadlockAnalyzer.hpp',
  'src/core/hpc/GdbRankCoordinator.hpp',
  'src/core/models/VariableTreeModel.hpp',
  'src/core/hpc/IBackendCoordinator.hpp'
]

processed_advanced_features_test_files = qt6.compile_moc(
  headers : advanced_features_test_moc_headers,
  dependencies: qt6_dep
)

gridlock_advanced_features_tests = executable('gridlock_advanced_features_tests',
  advanced_features_test_src,
  processed_advanced_features_test_files,
  include_directories : inc,
  dependencies : [qt6_dep, tomlplusplus_dep]
)

test('advanced_features_tests', gridlock_advanced_features_tests)

memory_export_test_src = [
  'tests/test_memory_export.cpp',
  'src/core/hpc/MemoryExporter.cpp'
]

memory_export_test_moc_headers = [
  'tests/test_memory_export.hpp'
]

processed_memory_export_test_files = qt6.compile_moc(
  headers : memory_export_test_moc_headers,
  dependencies: qt6_dep
)

gridlock_memory_export_tests = executable('gridlock_memory_export_tests',
  memory_export_test_src,
  processed_memory_export_test_files,
  include_directories : inc,
  dependencies : [qt6_dep, tomlplusplus_dep]
)

test('memory_export_tests', gridlock_memory_export_tests)

mi_parser_test_src = [
  'tests/test_mi_parser.cpp',
  'src/ui/GdbConsoleWidget.cpp'
]

mi_parser_test_moc_headers = [
  'src/ui/GdbConsoleWidget.hpp'
]

processed_mi_parser_test_files = qt6.compile_moc(
  headers : mi_parser_test_moc_headers,
  sources : 'tests/test_mi_parser.cpp',
  dependencies: qt6_dep
)

gridlock_mi_parser_tests = executable('gridlock_mi_parser_tests',
  mi_parser_test_src,
  processed_mi_parser_test_files,
  include_directories : inc,
  dependencies : [qt6_dep, tomlplusplus_dep]
)

test('mi_parser_tests', gridlock_mi_parser_tests)

tutorial_test_src = [
  'tests/test_tutorial.cpp',
  'src/core/utils/CliParser.cpp',
  'src/core/managers/ConfigManager.cpp',
  'src/ui/tutorial/TutorialDialog.cpp'
]

tutorial_test_moc_headers = [
  'src/ui/tutorial/TutorialDialog.hpp'
]

processed_tutorial_test_files = qt6.compile_moc(
  headers : tutorial_test_moc_headers,
  sources : 'tests/test_tutorial.cpp',
  dependencies: qt6_dep
)

gridlock_tutorial_tests = executable('gridlock_tutorial_tests',
  tutorial_test_src,
  processed_tutorial_test_files,
  include_directories : inc,
  dependencies : [qt6_dep, tomlplusplus_dep]
)

test('tutorial_tests', gridlock_tutorial_tests, env: ['QT_QPA_PLATFORM=offscreen'])


tutorial_e2e_test_src = [
  'tests/gui/test_tutorial_e2e.cpp'
]

# Get everything from ui_test_src except test_ui.cpp
foreach f : ui_test_src
  if f != 'tests/test_ui.cpp'
    tutorial_e2e_test_src += f
  endif
endforeach

tutorial_e2e_test_moc_headers = []

foreach h : ui_test_moc_headers
  if h != 'tests/test_ui.hpp'
    tutorial_e2e_test_moc_headers += h
  endif
endforeach

processed_tutorial_e2e_test_files = qt6.compile_moc(
  headers : tutorial_e2e_test_moc_headers,
  sources : 'tests/gui/test_tutorial_e2e.cpp',
  dependencies: qt6_dep
)

gridlock_tutorial_e2e_tests = executable('gridlock_tutorial_e2e_tests',
  tutorial_e2e_test_src,
  processed_tutorial_e2e_test_files,
  include_directories : inc,
  dependencies : [qt6_dep, tomlplusplus_dep]
)

test('tutorial_e2e_tests', gridlock_tutorial_e2e_tests, env: ['QT_QPA_PLATFORM=offscreen'], timeout: 180)

custom_target('mpi_mm_bin',
  input: 'tutorial/mpi_mm.c',
  output: 'mpi_mm',
  command: ['mpicc', '-g', '-O0', '@INPUT@', '-o', '@OUTPUT@'],
  build_by_default: true
)
project('GridLock', 'cpp',
  version : '0.1.0',
  default_options : ['warning_level=3', 'cpp_std=c++23'],
  subproject_dir : 'external')

# Dependencies
qt6 = import('qt6')
qt6_dep = dependency('qt6', modules: ['Core', 'Widgets', 'Gui', 'Test', 'Sql', 'Network', 'OpenGLWidgets'])
tomlplusplus_dep = dependency('tomlplusplus', fallback: ['tomlplusplus', 'tomlplusplus_dep'])

# Source files
src_files = [
  'src/main.cpp',
  'src/GridLockApp.cpp',
  'src/core/hpc/NativeMemoryReader.cpp',
  'src/core/hpc/MemoryBoundsValidator.cpp',
  'src/core/hpc/GdbRankCoordinator.cpp',
  'src/core/hpc/DapCoordinator.cpp',
  'src/core/managers/ConfigManager.cpp',
  'src/core/hpc/LspCoordinator.cpp',
  'src/ui/MainWindow.cpp',
  'src/ui/views/ServerRackView.cpp',
  'src/ui/views/SourceCodeView.cpp',
  'src/ui/EditorTabManager.cpp',
  'src/ui/widgets/ProjectExplorerWidget.cpp',
  'src/ui/dialogs/ProjectSettingsDialog.cpp',
  'src/ui/widgets/ExpressionEvaluatorWidget.cpp',
  'src/ui/views/DisassemblyView.cpp',
  'src/ui/widgets/ReferenceManualWidget.cpp',
  'src/ui/widgets/DifferentialGrid.cpp',
  'src/ui/docks/TerminalDock.cpp',
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
  'src/ui/DomainHeatmapWidget.cpp'
]

# Headers for MOC (Meta-Object Compiler)
moc_headers = [
  'src/GridLockApp.hpp',
  'src/core/hpc/GdbRankCoordinator.hpp',
  'src/core/hpc/DapCoordinator.hpp',
  'src/core/hpc/LspCoordinator.hpp',
  'src/ui/MainWindow.hpp',
  'src/ui/views/ServerRackView.hpp',
  'src/ui/views/SourceCodeView.hpp',
  'src/ui/EditorTabManager.hpp',
  'src/ui/widgets/ProjectExplorerWidget.hpp',
  'src/ui/dialogs/ProjectSettingsDialog.hpp',
  'src/ui/widgets/ExpressionEvaluatorWidget.hpp',
  'src/ui/views/DisassemblyView.hpp',
  'src/ui/widgets/ReferenceManualWidget.hpp',
  'src/ui/widgets/DifferentialGrid.hpp',
  'src/ui/docks/TerminalDock.hpp',
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
  'src/core/models/VariableTreeModel.hpp',
  'src/ui/docks/VariablesDockWidget.hpp',
  'src/ui/DomainHeatmapWidget.hpp'
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
  'src/core/hpc/LspCoordinator.cpp'
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
  'src/ui/EditorTabManager.cpp',
  'src/ui/widgets/ProjectExplorerWidget.cpp',
  'src/ui/dialogs/ProjectSettingsDialog.cpp',
  'src/ui/widgets/ExpressionEvaluatorWidget.cpp',
  'src/ui/views/ServerRackView.cpp',
  'src/ui/views/DisassemblyView.cpp',
  'src/ui/widgets/ReferenceManualWidget.cpp',
  'src/ui/widgets/DifferentialGrid.cpp',
  'src/ui/docks/TerminalDock.cpp',
  'src/ui/GdbConsoleWidget.cpp',
  'src/core/hpc/GdbRankCoordinator.cpp',
  'src/core/hpc/DapCoordinator.cpp',
  'src/core/managers/ConfigManager.cpp',
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
  'src/ui/docks/VariablesDockWidget.cpp'
]

ui_test_moc_headers = [
  'tests/test_ui.hpp',
  'src/ui/MainWindow.hpp',
  'src/ui/views/SourceCodeView.hpp',
  'src/ui/EditorTabManager.hpp',
  'src/ui/widgets/ProjectExplorerWidget.hpp',
  'src/ui/dialogs/ProjectSettingsDialog.hpp',
  'src/ui/widgets/ExpressionEvaluatorWidget.hpp',
  'src/ui/views/ServerRackView.hpp',
  'src/ui/views/DisassemblyView.hpp',
  'src/ui/widgets/ReferenceManualWidget.hpp',
  'src/ui/widgets/DifferentialGrid.hpp',
  'src/ui/docks/TerminalDock.hpp',
  'src/ui/GdbConsoleWidget.hpp',
  'src/core/hpc/GdbRankCoordinator.hpp',
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
  'src/core/models/VariableTreeModel.hpp',
  'src/ui/docks/VariablesDockWidget.hpp'
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
  'src/core/managers/ConfigManager.cpp',
  'src/ui/views/SourceCodeView.cpp'
]

commands_test_moc_headers = [
  'tests/test_commands.hpp',
  'src/core/hpc/GdbRankCoordinator.hpp',
  'src/core/hpc/DapCoordinator.hpp',
  'src/core/hpc/IBackendCoordinator.hpp',
  'src/ui/views/SourceCodeView.hpp'
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
  'src/core/commands/DebugCommands.cpp',
  'src/core/managers/ConfigManager.cpp',
  'src/ui/views/SourceCodeView.cpp'
]

dap_commands_test_moc_headers = [
  'tests/test_dap_commands.hpp',
  'src/core/hpc/DapCoordinator.hpp',
  'src/core/hpc/IBackendCoordinator.hpp',
  'src/ui/views/SourceCodeView.hpp'
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

custom_target('mpi_mm_bin',
  input: 'tests/mpi_mm.c',
  output: 'mpi_mm_bin',
  command: ['mpicxx', '@INPUT@', '-g', '-O0', '-o', '@OUTPUT@'],
  build_by_default: true)
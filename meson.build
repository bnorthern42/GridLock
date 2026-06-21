project('GridLock', 'cpp',
  version : '0.1.0',
  default_options : ['warning_level=3', 'cpp_std=c++23'],
  subproject_dir : 'external')

# Dependencies
qt6 = import('qt6')
qt6_dep = dependency('qt6', modules: ['Core', 'Widgets', 'Gui', 'Test', 'Sql', 'Network'])
tomlplusplus_dep = dependency('tomlplusplus', fallback: ['tomlplusplus', 'tomlplusplus_dep'])

# Source files
src_files = [
  'src/main.cpp',
  'src/GridLockApp.cpp',
  'src/GdbRankCoordinator.cpp',
  'src/core/ConfigManager.cpp',
  'src/core/LspCoordinator.cpp',
  'src/ui/MainWindow.cpp',
  'src/ui/ServerRackView.cpp',
  'src/ui/SourceCodeView.cpp',
  'src/ui/EditorTabManager.cpp',
  'src/ui/ProjectExplorerWidget.cpp',
  'src/ui/ProjectSettingsDialog.cpp',
  'src/ui/ExpressionEvaluatorWidget.cpp',
  'src/ui/DisassemblyView.cpp',
  'src/ui/ReferenceManualWidget.cpp',
  'src/ui/DifferentialGrid.cpp',
  'src/ui/TerminalDock.cpp',
  'src/ui/GdbConsoleWidget.cpp',
  'src/backend/TestGenerator.cpp',
  'src/backend/GridLockAutomationRunner.cpp',
  'src/ui/MemView.cpp',
  'src/ui/RegisterView.cpp',
  'src/ui/PreferencesDialog.cpp',
  'src/core/MockHpcBackend.cpp',
  'src/ui/SpackManager.cpp',
  'src/core/commands/DebugCommands.cpp',
  'src/core/ShortcutManager.cpp',
  'src/core/DocsetManager.cpp',
  'src/ui/ThemeManager.cpp',
  'src/core/VariableTreeModel.cpp',
  'src/ui/VariablesDockWidget.cpp'
]

# Headers for MOC (Meta-Object Compiler)
moc_headers = [
  'src/GridLockApp.hpp',
  'src/GdbRankCoordinator.hpp',
  'src/core/LspCoordinator.hpp',
  'src/ui/MainWindow.hpp',
  'src/ui/ServerRackView.hpp',
  'src/ui/SourceCodeView.hpp',
  'src/ui/EditorTabManager.hpp',
  'src/ui/ProjectExplorerWidget.hpp',
  'src/ui/ProjectSettingsDialog.hpp',
  'src/ui/ExpressionEvaluatorWidget.hpp',
  'src/ui/DisassemblyView.hpp',
  'src/ui/ReferenceManualWidget.hpp',
  'src/ui/DifferentialGrid.hpp',
  'src/ui/TerminalDock.hpp',
  'src/ui/GdbConsoleWidget.hpp',
  'src/backend/GridLockAutomationRunner.hpp',
  'src/ui/MemView.hpp',
  'src/ui/RegisterView.hpp',
  'src/ui/PreferencesDialog.hpp',
  'src/core/HpcBackend.hpp',
  'src/core/MockHpcBackend.hpp',
  'src/ui/SpackManager.hpp',
  'src/core/ShortcutManager.hpp',
  'src/core/DocsetManager.hpp',
  'src/core/VariableTreeModel.hpp',
  'src/ui/VariablesDockWidget.hpp'
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
  'src/GdbRankCoordinator.cpp',
  'src/core/ConfigManager.cpp'
]

test_moc_headers = [
  'tests/test_coordinator.hpp',
  'src/GdbRankCoordinator.hpp'
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
  'src/core/LspCoordinator.cpp'
]

lsp_test_moc_headers = [
  'tests/test_lsp.hpp',
  'src/core/LspCoordinator.hpp'
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
  'src/ui/SourceCodeView.cpp',
  'src/ui/EditorTabManager.cpp',
  'src/ui/ProjectExplorerWidget.cpp',
  'src/ui/ProjectSettingsDialog.cpp',
  'src/ui/ExpressionEvaluatorWidget.cpp',
  'src/ui/ServerRackView.cpp',
  'src/ui/DisassemblyView.cpp',
  'src/ui/ReferenceManualWidget.cpp',
  'src/ui/DifferentialGrid.cpp',
  'src/ui/TerminalDock.cpp',
  'src/ui/GdbConsoleWidget.cpp',
  'src/GdbRankCoordinator.cpp',
  'src/core/ConfigManager.cpp',
  'src/core/LspCoordinator.cpp',
  'src/ui/MemView.cpp',
  'src/ui/RegisterView.cpp',
  'src/ui/PreferencesDialog.cpp',
  'src/core/MockHpcBackend.cpp',
  'src/ui/SpackManager.cpp',
  'src/core/commands/DebugCommands.cpp',
  'src/core/ShortcutManager.cpp',
  'src/core/DocsetManager.cpp',
  'src/ui/ThemeManager.cpp',
  'src/core/VariableTreeModel.cpp',
  'src/ui/VariablesDockWidget.cpp'
]

ui_test_moc_headers = [
  'tests/test_ui.hpp',
  'src/ui/MainWindow.hpp',
  'src/ui/SourceCodeView.hpp',
  'src/ui/EditorTabManager.hpp',
  'src/ui/ProjectExplorerWidget.hpp',
  'src/ui/ProjectSettingsDialog.hpp',
  'src/ui/ExpressionEvaluatorWidget.hpp',
  'src/ui/ServerRackView.hpp',
  'src/ui/DisassemblyView.hpp',
  'src/ui/ReferenceManualWidget.hpp',
  'src/ui/DifferentialGrid.hpp',
  'src/ui/TerminalDock.hpp',
  'src/ui/GdbConsoleWidget.hpp',
  'src/GdbRankCoordinator.hpp',
  'src/core/LspCoordinator.hpp',
  'src/ui/MemView.hpp',
  'src/ui/RegisterView.hpp',
  'src/ui/PreferencesDialog.hpp',
  'src/core/HpcBackend.hpp',
  'src/core/MockHpcBackend.hpp',
  'src/ui/SpackManager.hpp',
  'src/core/ShortcutManager.hpp',
  'src/core/DocsetManager.hpp',
  'src/core/VariableTreeModel.hpp',
  'src/ui/VariablesDockWidget.hpp'
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
  'src/core/ConfigManager.cpp'
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
  'src/core/DocsetManager.cpp',
  'src/core/ConfigManager.cpp'
]

docset_test_moc_headers = [
  'tests/test_docset.hpp',
  'src/core/DocsetManager.hpp'
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
  'src/GdbRankCoordinator.cpp',
  'src/core/ConfigManager.cpp',
  'src/ui/SourceCodeView.cpp'
]

commands_test_moc_headers = [
  'tests/test_commands.hpp',
  'src/GdbRankCoordinator.hpp',
  'src/ui/SourceCodeView.hpp'
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

custom_target('mpi_mm_bin',
  input: 'tests/mpi_mm.c',
  output: 'mpi_mm_bin',
  command: ['mpicxx', '@INPUT@', '-g', '-O0', '-o', '@OUTPUT@'],
  build_by_default: true)
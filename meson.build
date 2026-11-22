project('GridLock', 'cpp', 'c',
  version : '0.8.7',
  default_options : ['warning_level=3', 'cpp_std=c++23'],
  subproject_dir : 'external')

add_project_arguments('-Wall', '-Wextra', '-Wpedantic', '-Wunused-variable', '-Wunused-parameter', '-Wno-sfinae-incomplete', language: 'cpp')

# Dependencies
qt6 = import('qt6')
qt6_dep = dependency('qt6', modules: ['Core', 'Widgets', 'Gui', 'Test', 'Sql', 'Network', 'OpenGLWidgets', 'Concurrent', 'Qml', 'Svg'])
tomlplusplus_dep = dependency('tomlplusplus', fallback: ['tomlplusplus', 'tomlplusplus_dep'])
qtermwidget_dep = dependency('qtermwidget6')
acss_dep = dependency('QtAdvancedStylesheets', method: 'cmake', modules: 'QtAdvancedStylesheets::qtadvancedcss', required: false)
if not acss_dep.found()
  cpp = meson.get_compiler('cpp')
  acss_lib = cpp.find_library('qtadvancedcss', dirs: [meson.current_source_dir() + '/external/Qt-Advanced-Stylesheets/build/lib'])
  acss_dep = declare_dependency(dependencies: acss_lib, include_directories: include_directories('external/Qt-Advanced-Stylesheets/src'))
endif
# Version generation
conf_data = configuration_data()
conf_data.set('GRIDLOCK_VERSION', meson.project_version())
configure_file(input : 'src/core/VersionConfig.hpp.in',
               output : 'VersionConfig.hpp',
               configuration : conf_data)

# Source files
src_files = [
  'src/main.cpp', 'src/GridLockApp.cpp', 'src/core/Version.cpp',
  'src/core/hpc/memory/NativeMemoryReader.cpp', 'src/core/hpc/memory/MemoryBoundsValidator.cpp',
  'src/core/hpc/gdb/GdbRankCoordinator.cpp', 'src/core/hpc/gdb/GdbMiParser.cpp',
  'src/core/hpc/gdb/GdbRankInspection.cpp', 'src/core/hpc/gdb/GdbRankCommands.cpp',
  'src/core/hpc/dap/DapCoordinator.cpp', 'src/core/hpc/gdb/MiSanitizer.cpp',
  'src/core/hpc/system/TelemetryObfuscator.cpp', 'src/core/hpc/memory/MemoryExporter.cpp',
  'src/core/hpc/memory/MemoryDiffer.cpp', 'src/core/managers/ConfigManager.cpp', 'src/core/managers/ConfigManagerToml.cpp', 'src/core/managers/ConfigManagerWorkspace.cpp',
  'src/core/managers/SessionManager.cpp', 'src/core/hpc/lsp/LspCoordinator.cpp',
  'src/core/hpc/analysis/DeadlockAnalyzer.cpp', 'src/core/hpc/system/ProcessScanner.cpp',
  'src/ui/MainWindow.cpp', 'src/ui/MainWindowLifecycle.cpp', 'src/ui/MainWindowWorkspace.cpp',
  'src/ui/MainWindowToolbar.cpp', 'src/ui/MainWindowNavigation.cpp', 'src/ui/MainWindowBinaryNav.cpp',
  'src/ui/MainWindowCohesion.cpp', 'src/ui/MainWindowPerspectives.cpp',
  'src/ui/MainWindowDocks.cpp', 'src/ui/MainWindowDebugger.cpp',
  'src/ui/MainWindowExecution.cpp', 'src/ui/MainWindowInspection.cpp',
  'src/ui/MainWindowBuild.cpp',
  'src/ui/views/ServerRackView.cpp', 'src/ui/views/SourceCodeView.cpp', 'src/ui/views/SourceCodeViewInteraction.cpp',
  'src/ui/views/EditorSyntaxHighlighter.cpp', 'src/ui/views/CsvTablePreviewWidget.cpp',
  'src/ui/dialogs/plotter/ArrayPlotCanvas.cpp', 'src/ui/dialogs/plotter/ArrayPlotDialog.cpp', 'src/ui/dialogs/plotter/ArrayPlotExport.cpp', 'src/ui/dialogs/plotter/TensorSliceTableModel.cpp',
  'src/ui/dialogs/graph/PointerGraphDialog.cpp', 'src/ui/dialogs/graph/PointerGraphEvaluator.cpp', 'src/ui/dialogs/graph/PointerGraphLayout.cpp',
  'src/ui/dialogs/calculator/CalculatorEngine.cpp', 'src/ui/dialogs/calculator/ProgrammerCalculatorDialog.cpp',
  'src/ui/dialogs/debugger/AttachProcessDialog.cpp', 'src/ui/dialogs/debugger/CheckpointDialog.cpp', 'src/ui/dialogs/debugger/ConditionalBreakpointDialog.cpp', 'src/ui/dialogs/debugger/SignalCatchpointDialog.cpp', 'src/ui/dialogs/debugger/XrefInspectorDialog.cpp',
  'src/ui/dialogs/project/ProjectSettingsDialog.cpp', 'src/ui/dialogs/project/ProjectWizard.cpp', 'src/ui/dialogs/search/CommandPaletteDialog.cpp',
  'src/ui/EditorTabManager.cpp', 'src/ui/widgets/ProjectExplorerWidget.cpp', 'src/ui/widgets/ProjectExplorerActions.cpp',
  'src/ui/widgets/FileIconProvider.cpp', 'src/ui/widgets/ExpressionEvaluatorWidget.cpp', 'src/ui/widgets/InlineFindBar.cpp', 'src/ui/widgets/HpcConsoleFormatting.cpp',
  'src/ui/views/DisassemblyView.cpp', 'src/ui/views/DisassemblyViewActions.cpp', 'src/ui/views/DisassemblyAnalysis.cpp',
  'src/ui/views/DisassemblyDecompiler.cpp', 'src/ui/views/DisassemblyDecompilerPasses.cpp', 'src/ui/widgets/ReferenceManualWidget.cpp',
  'src/ui/widgets/DifferentialGrid.cpp', 'src/ui/widgets/MpiNetworkLogWidget.cpp',
  'src/ui/docks/TerminalDockWidget.cpp', 'src/ui/docks/CallStackDockWidget.cpp',
  'src/ui/widgets/MpiDiagnosticsWidget.cpp', 'src/ui/GdbConsoleWidget.cpp',
  'src/backend/TestGenerator.cpp', 'src/backend/GridLockAutomationRunner.cpp',
  'src/ui/views/MemView.cpp', 'src/ui/views/MemViewRender.cpp', 'src/ui/views/MemoryProfilerWidget.cpp', 'src/ui/views/MemoryProfilerChart.cpp', 'src/ui/views/RegisterView.cpp',
  'src/ui/dialogs/preferences/PreferencesDialog.cpp', 'src/ui/dialogs/preferences/PreferencesDialogChrome.cpp', 'src/ui/dialogs/preferences/AppearanceSettingsPage.cpp', 'src/ui/dialogs/preferences/EditingBehaviorPages.cpp', 'src/ui/dialogs/preferences/ShortcutItemDelegate.cpp',
  'src/ui/dialogs/preferences/KeybindingsSettingsPage.cpp', 'src/ui/dialogs/preferences/DebuggerSettingsPage.cpp', 'src/ui/dialogs/preferences/HpcSettingsPages.cpp', 'src/ui/dialogs/preferences/SlurmBatchParser.cpp', 'src/ui/dialogs/preferences/TerminalDocsetsPages.cpp',
  'src/core/hpc/analysis/MockHpcBackend.cpp', 'src/core/managers/SpackManager.cpp',
  'src/core/commands/DebugCommands.cpp', 'src/core/managers/ShortcutManager.cpp',
  'src/core/managers/DocsetManager.cpp', 'src/core/managers/ThemeManager.cpp',
  'src/core/models/VariableTreeModel.cpp', 'src/ui/docks/VariablesDockWidget.cpp',
  'src/core/utils/CliParser.cpp',
  'src/core/utils/ElfBinaryInspector.cpp', 'src/core/utils/SearchEverywhereEngine.cpp', 'src/core/utils/ClangdLspClient.cpp', 'src/core/utils/ClangdLspNavigation.cpp',
  'src/ui/tutorial/TutorialDialog.cpp',
]

# Headers for MOC (Meta-Object Compiler)
moc_headers = [
  'src/GridLockApp.hpp', 'src/core/hpc/gdb/GdbRankCoordinator.hpp',
  'src/core/hpc/dap/DapCoordinator.hpp', 'src/core/hpc/system/TelemetryObfuscator.hpp',
  'src/core/hpc/analysis/DeadlockAnalyzer.hpp', 'src/core/hpc/lsp/LspCoordinator.hpp',
  'src/ui/MainWindow.hpp', 'src/ui/views/ServerRackView.hpp',
  'src/ui/views/SourceCodeView.hpp', 'src/ui/views/EditorSyntaxHighlighter.hpp',
  'src/ui/views/CsvTablePreviewWidget.hpp',
  'src/ui/dialogs/plotter/ArrayPlotCanvas.hpp', 'src/ui/dialogs/plotter/ArrayPlotDialog.hpp', 'src/ui/dialogs/plotter/TensorSliceTableModel.hpp',
  'src/ui/dialogs/graph/PointerGraphDialog.hpp', 'src/ui/dialogs/calculator/ProgrammerCalculatorDialog.hpp',
  'src/ui/dialogs/debugger/CheckpointDialog.hpp', 'src/ui/dialogs/debugger/ConditionalBreakpointDialog.hpp', 'src/ui/dialogs/debugger/SignalCatchpointDialog.hpp', 'src/ui/dialogs/debugger/XrefInspectorDialog.hpp',
  'src/ui/dialogs/project/ProjectSettingsDialog.hpp', 'src/ui/dialogs/project/ProjectWizard.hpp', 'src/ui/dialogs/search/CommandPaletteDialog.hpp',
  'src/ui/EditorTabManager.hpp', 'src/ui/widgets/ProjectExplorerWidget.hpp',
  'src/ui/widgets/ExpressionEvaluatorWidget.hpp', 'src/ui/widgets/InlineFindBar.hpp',
  'src/ui/views/DisassemblyView.hpp', 'src/ui/widgets/ReferenceManualWidget.hpp',
  'src/ui/widgets/DifferentialGrid.hpp', 'src/ui/widgets/MpiNetworkLogWidget.hpp',
  'src/ui/docks/TerminalDockWidget.hpp', 'src/ui/docks/CallStackDockWidget.hpp',
  'src/ui/widgets/MpiDiagnosticsWidget.hpp', 'src/ui/GdbConsoleWidget.hpp',
  'src/core/hpc/IBackendCoordinator.hpp', 'src/backend/GridLockAutomationRunner.hpp',
  'src/ui/views/MemView.hpp', 'src/ui/views/MemoryProfilerWidget.hpp', 'src/ui/views/RegisterView.hpp',
  'src/ui/dialogs/preferences/PreferencesDialog.hpp', 'src/ui/dialogs/preferences/AppearanceSettingsPage.hpp', 'src/ui/dialogs/preferences/EditingBehaviorPages.hpp', 'src/ui/dialogs/preferences/KeybindingsSettingsPage.hpp',
  'src/ui/dialogs/preferences/DebuggerSettingsPage.hpp', 'src/ui/dialogs/preferences/HpcSettingsPages.hpp', 'src/ui/dialogs/preferences/TerminalDocsetsPages.hpp', 'src/core/hpc/HpcBackend.hpp',
  'src/core/hpc/analysis/MockHpcBackend.hpp', 'src/core/managers/SpackManager.hpp',
  'src/core/managers/ShortcutManager.hpp', 'src/core/managers/DocsetManager.hpp',
  'src/core/managers/ThemeManager.hpp', 'src/core/managers/SessionManager.hpp',
  'src/core/models/VariableTreeModel.hpp', 'src/ui/docks/VariablesDockWidget.hpp',
  'src/ui/tutorial/TutorialDialog.hpp', 'src/core/utils/ClangdLspClient.hpp',
]

# Process Qt6 files
processed_qt_files = qt6.compile_moc(
  headers : moc_headers,
  dependencies: qt6_dep
)

compiled_resources = qt6.compile_resources(
  name: 'gridlock_resources',
  sources: ['resources/gridlock.qrc', 'resources/styles.qrc']
)

# Include directories
inc = include_directories('src')

# Executable
executable('gridlock',
  src_files,
  processed_qt_files,
  compiled_resources,
  include_directories : inc,
  dependencies : [qt6_dep, tomlplusplus_dep, acss_dep, qtermwidget_dep],
  build_rpath : '$ORIGIN/../lib:' + meson.current_source_dir() + '/external/Qt-Advanced-Stylesheets/build/lib',
  install_rpath : '$ORIGIN/../lib',
  install : true)

install_data('data/gridlock.desktop', 
             install_dir: get_option('datadir') / 'applications')

install_data('resources/icon.png', 
             rename: 'gridlock.png',
             install_dir: get_option('datadir') / 'icons/hicolor/512x512/apps')


# Test executable
test_src = [
  'tests/hpc/test_coordinator.cpp',
  'src/core/commands/DebugCommands.cpp',
  'src/core/hpc/gdb/GdbRankCoordinator.cpp', 'src/core/hpc/gdb/GdbMiParser.cpp',
  'src/core/hpc/gdb/GdbRankInspection.cpp', 'src/core/hpc/gdb/GdbRankCommands.cpp',
  'src/core/hpc/dap/DapCoordinator.cpp', 'src/core/hpc/memory/NativeMemoryReader.cpp',
  'src/core/hpc/system/ProcessScanner.cpp', 'src/core/managers/ConfigManager.cpp', 'src/core/managers/ConfigManagerToml.cpp', 'src/core/managers/ConfigManagerWorkspace.cpp'
]

test_moc_headers = [
  'tests/hpc/test_coordinator.hpp',
  'src/core/hpc/gdb/GdbRankCoordinator.hpp',
  'src/core/hpc/dap/DapCoordinator.hpp',
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
  dependencies : [qt6_dep, tomlplusplus_dep],
  build_by_default : false
)

test('coordinator_tests', gridlock_tests, env: ['QT_QPA_PLATFORM=offscreen'])

lsp_test_src = [
  'tests/hpc/test_lsp.cpp',
  'src/core/hpc/lsp/LspCoordinator.cpp', 'src/core/utils/ClangdLspNavigation.cpp',
  'src/core/managers/ConfigManager.cpp', 'src/core/managers/ConfigManagerToml.cpp', 'src/core/managers/ConfigManagerWorkspace.cpp', 'src/ui/views/SourceCodeView.cpp', 'src/ui/views/SourceCodeViewInteraction.cpp',
  'src/ui/views/EditorSyntaxHighlighter.cpp', 'src/ui/dialogs/debugger/ConditionalBreakpointDialog.cpp',
  'src/ui/widgets/InlineFindBar.cpp'
]

lsp_test_moc_headers = [
  'tests/hpc/test_lsp.hpp',
  'src/core/hpc/lsp/LspCoordinator.hpp', 'src/core/utils/ClangdLspClient.hpp',
  'src/ui/views/SourceCodeView.hpp', 'src/ui/views/EditorSyntaxHighlighter.hpp',
  'src/ui/dialogs/debugger/ConditionalBreakpointDialog.hpp', 'src/ui/widgets/InlineFindBar.hpp'
]

processed_lsp_test_files = qt6.compile_moc(
  headers : lsp_test_moc_headers,
  dependencies: qt6_dep
)

gridlock_lsp_tests = executable('gridlock_lsp_tests',
  lsp_test_src,
  processed_lsp_test_files,
  include_directories : inc,
  dependencies : [qt6_dep, tomlplusplus_dep],
  build_by_default : false
)

test('lsp_tests', gridlock_lsp_tests, env: ['QT_QPA_PLATFORM=offscreen'])

ui_test_src = [
  'tests/ui/test_ui.cpp', 'tests/ui/test_ui_aux.cpp',
  'tests/ui/test_ui_cohesion.cpp', 'tests/ui/test_ui_capture.cpp',
  'tests/ui/test_ui_debugger.cpp', 'tests/ui/test_ui_viz_and_ux.cpp',
  'src/ui/MainWindow.cpp', 'src/ui/MainWindowLifecycle.cpp', 'src/ui/MainWindowWorkspace.cpp',
  'src/ui/MainWindowToolbar.cpp', 'src/ui/MainWindowNavigation.cpp', 'src/ui/MainWindowBinaryNav.cpp',
  'src/ui/MainWindowCohesion.cpp', 'src/ui/MainWindowPerspectives.cpp',
  'src/ui/MainWindowDocks.cpp', 'src/ui/MainWindowDebugger.cpp',
  'src/ui/MainWindowExecution.cpp', 'src/ui/MainWindowInspection.cpp',
  'src/ui/MainWindowBuild.cpp',
  'src/ui/views/SourceCodeView.cpp', 'src/ui/views/SourceCodeViewInteraction.cpp', 'src/ui/views/EditorSyntaxHighlighter.cpp', 'src/ui/views/CsvTablePreviewWidget.cpp',
  'src/ui/dialogs/plotter/ArrayPlotCanvas.cpp', 'src/ui/dialogs/plotter/ArrayPlotDialog.cpp', 'src/ui/dialogs/plotter/ArrayPlotExport.cpp', 'src/ui/dialogs/plotter/TensorSliceTableModel.cpp',
  'src/ui/dialogs/graph/PointerGraphDialog.cpp', 'src/ui/dialogs/graph/PointerGraphEvaluator.cpp', 'src/ui/dialogs/graph/PointerGraphLayout.cpp',
  'src/ui/dialogs/calculator/CalculatorEngine.cpp', 'src/ui/dialogs/calculator/ProgrammerCalculatorDialog.cpp',
  'src/ui/dialogs/debugger/AttachProcessDialog.cpp', 'src/ui/dialogs/debugger/CheckpointDialog.cpp', 'src/ui/dialogs/debugger/ConditionalBreakpointDialog.cpp', 'src/ui/dialogs/debugger/SignalCatchpointDialog.cpp', 'src/ui/dialogs/debugger/XrefInspectorDialog.cpp',
  'src/ui/dialogs/project/ProjectSettingsDialog.cpp', 'src/ui/dialogs/project/ProjectWizard.cpp', 'src/ui/dialogs/search/CommandPaletteDialog.cpp',
  'src/ui/EditorTabManager.cpp', 'src/ui/widgets/ProjectExplorerWidget.cpp', 'src/ui/widgets/ProjectExplorerActions.cpp',
  'src/ui/widgets/FileIconProvider.cpp', 'src/ui/tutorial/TutorialDialog.cpp', 'src/ui/widgets/ExpressionEvaluatorWidget.cpp', 'src/ui/widgets/InlineFindBar.cpp', 'src/ui/widgets/HpcConsoleFormatting.cpp',
  'src/ui/views/ServerRackView.cpp', 'src/ui/views/DisassemblyView.cpp', 'src/ui/views/DisassemblyViewActions.cpp',
  'src/ui/views/DisassemblyAnalysis.cpp', 'src/ui/views/DisassemblyDecompiler.cpp', 'src/ui/views/DisassemblyDecompilerPasses.cpp',
  'src/ui/widgets/ReferenceManualWidget.cpp', 'src/ui/widgets/DifferentialGrid.cpp',
  'src/ui/widgets/MpiNetworkLogWidget.cpp',
  'src/ui/docks/TerminalDockWidget.cpp', 'src/ui/docks/CallStackDockWidget.cpp',
  'src/ui/widgets/MpiDiagnosticsWidget.cpp', 'src/ui/GdbConsoleWidget.cpp',
  'src/core/hpc/gdb/GdbRankCoordinator.cpp', 'src/core/hpc/gdb/GdbMiParser.cpp',
  'src/core/hpc/gdb/GdbRankInspection.cpp', 'src/core/hpc/gdb/GdbRankCommands.cpp',
  'src/core/hpc/analysis/DeadlockAnalyzer.cpp', 'src/core/hpc/system/ProcessScanner.cpp', 'src/core/hpc/dap/DapCoordinator.cpp',
  'src/core/hpc/memory/NativeMemoryReader.cpp', 'src/core/hpc/memory/MemoryExporter.cpp',
  'src/core/hpc/memory/MemoryDiffer.cpp', 'src/core/managers/ConfigManager.cpp', 'src/core/managers/ConfigManagerToml.cpp', 'src/core/managers/ConfigManagerWorkspace.cpp',
  'src/core/managers/SessionManager.cpp', 'src/core/hpc/lsp/LspCoordinator.cpp',
  'src/ui/views/MemView.cpp', 'src/ui/views/MemViewRender.cpp', 'src/ui/views/MemoryProfilerWidget.cpp', 'src/ui/views/MemoryProfilerChart.cpp', 'src/ui/views/RegisterView.cpp',
  'src/ui/dialogs/preferences/PreferencesDialog.cpp', 'src/ui/dialogs/preferences/PreferencesDialogChrome.cpp', 'src/ui/dialogs/preferences/AppearanceSettingsPage.cpp', 'src/ui/dialogs/preferences/EditingBehaviorPages.cpp', 'src/ui/dialogs/preferences/ShortcutItemDelegate.cpp',
  'src/ui/dialogs/preferences/KeybindingsSettingsPage.cpp', 'src/ui/dialogs/preferences/DebuggerSettingsPage.cpp', 'src/ui/dialogs/preferences/HpcSettingsPages.cpp', 'src/ui/dialogs/preferences/SlurmBatchParser.cpp', 'src/ui/dialogs/preferences/TerminalDocsetsPages.cpp',
  'src/core/hpc/analysis/MockHpcBackend.cpp', 'src/core/managers/SpackManager.cpp',
  'src/core/commands/DebugCommands.cpp', 'src/core/managers/ShortcutManager.cpp',
  'src/core/managers/DocsetManager.cpp', 'src/core/managers/ThemeManager.cpp',
  'src/core/models/VariableTreeModel.cpp', 'src/ui/docks/VariablesDockWidget.cpp',
  'src/core/utils/ElfBinaryInspector.cpp', 'src/core/utils/SearchEverywhereEngine.cpp', 'src/core/utils/ClangdLspClient.cpp', 'src/core/utils/ClangdLspNavigation.cpp',
  'src/core/Version.cpp',
]

ui_test_moc_headers = [
  'tests/ui/test_ui.hpp', 'src/ui/MainWindow.hpp',
  'src/ui/views/SourceCodeView.hpp', 'src/ui/views/EditorSyntaxHighlighter.hpp',
  'src/ui/views/CsvTablePreviewWidget.hpp',
  'src/ui/dialogs/plotter/ArrayPlotCanvas.hpp', 'src/ui/dialogs/plotter/ArrayPlotDialog.hpp', 'src/ui/dialogs/plotter/TensorSliceTableModel.hpp',
  'src/ui/dialogs/graph/PointerGraphDialog.hpp', 'src/ui/dialogs/calculator/ProgrammerCalculatorDialog.hpp',
  'src/ui/dialogs/debugger/CheckpointDialog.hpp', 'src/ui/dialogs/debugger/ConditionalBreakpointDialog.hpp', 'src/ui/dialogs/debugger/SignalCatchpointDialog.hpp', 'src/ui/dialogs/debugger/XrefInspectorDialog.hpp',
  'src/ui/dialogs/project/ProjectSettingsDialog.hpp', 'src/ui/dialogs/project/ProjectWizard.hpp', 'src/ui/dialogs/search/CommandPaletteDialog.hpp',
  'src/ui/EditorTabManager.hpp', 'src/ui/widgets/ProjectExplorerWidget.hpp',
  'src/ui/tutorial/TutorialDialog.hpp',
  'src/ui/widgets/ExpressionEvaluatorWidget.hpp', 'src/ui/widgets/InlineFindBar.hpp', 'src/ui/views/ServerRackView.hpp',
  'src/ui/views/DisassemblyView.hpp', 'src/ui/widgets/ReferenceManualWidget.hpp',
  'src/ui/widgets/DifferentialGrid.hpp', 'src/ui/widgets/MpiNetworkLogWidget.hpp',
  'src/ui/docks/TerminalDockWidget.hpp', 'src/ui/docks/CallStackDockWidget.hpp',
  'src/ui/widgets/MpiDiagnosticsWidget.hpp', 'src/ui/GdbConsoleWidget.hpp',
  'src/core/hpc/gdb/GdbRankCoordinator.hpp', 'src/core/hpc/analysis/DeadlockAnalyzer.hpp',
  'src/core/hpc/dap/DapCoordinator.hpp', 'src/core/hpc/IBackendCoordinator.hpp',
  'src/core/hpc/lsp/LspCoordinator.hpp', 'src/ui/views/MemView.hpp', 'src/ui/views/MemoryProfilerWidget.hpp',
  'src/ui/views/RegisterView.hpp', 'src/ui/dialogs/preferences/PreferencesDialog.hpp', 'src/ui/dialogs/preferences/AppearanceSettingsPage.hpp', 'src/ui/dialogs/preferences/EditingBehaviorPages.hpp', 'src/ui/dialogs/preferences/KeybindingsSettingsPage.hpp',
  'src/ui/dialogs/preferences/DebuggerSettingsPage.hpp', 'src/ui/dialogs/preferences/HpcSettingsPages.hpp', 'src/ui/dialogs/preferences/TerminalDocsetsPages.hpp',
  'src/core/hpc/HpcBackend.hpp', 'src/core/hpc/analysis/MockHpcBackend.hpp',
  'src/core/managers/SpackManager.hpp', 'src/core/managers/ShortcutManager.hpp',
  'src/core/managers/DocsetManager.hpp', 'src/core/managers/ThemeManager.hpp',
  'src/core/managers/SessionManager.hpp', 'src/core/models/VariableTreeModel.hpp',
  'src/ui/docks/VariablesDockWidget.hpp', 'src/core/utils/ClangdLspClient.hpp',
]

processed_ui_test_files = qt6.compile_moc(
  headers : ui_test_moc_headers,
  dependencies: qt6_dep
)

gridlock_ui_tests = executable('gridlock_ui_tests',
  ui_test_src,
  processed_ui_test_files,
  compiled_resources,
  include_directories : inc,
  dependencies : [qt6_dep, tomlplusplus_dep, acss_dep, qtermwidget_dep],
  build_by_default : false
)

test('ui_tests', gridlock_ui_tests, env: ['QT_QPA_PLATFORM=offscreen'], timeout: 120)

config_test_src = [
  'tests/core/test_config.cpp',
  'src/core/managers/ConfigManager.cpp', 'src/core/managers/ConfigManagerToml.cpp', 'src/core/managers/ConfigManagerWorkspace.cpp',
  'src/core/managers/SessionManager.cpp'
]

config_test_moc_headers = [
  'tests/core/test_config.hpp',
  'src/core/managers/SessionManager.hpp'
]

processed_config_test_files = qt6.compile_moc(
  headers : config_test_moc_headers,
  dependencies: qt6_dep
)

gridlock_config_tests = executable('gridlock_config_tests',
  config_test_src,
  processed_config_test_files,
  include_directories : inc,
  dependencies : [qt6_dep, tomlplusplus_dep],
  build_by_default : false
)

test('config_tests', gridlock_config_tests, env: ['QT_QPA_PLATFORM=offscreen'])

docset_test_src = [
  'tests/core/test_docset.cpp',
  'src/core/managers/DocsetManager.cpp',
  'src/core/managers/ConfigManager.cpp', 'src/core/managers/ConfigManagerToml.cpp', 'src/core/managers/ConfigManagerWorkspace.cpp'
]

docset_test_moc_headers = [
  'tests/core/test_docset.hpp',
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
  dependencies : [qt6_dep, tomlplusplus_dep],
  build_by_default : false
)

test('docset_tests', gridlock_docset_tests, env: ['QT_QPA_PLATFORM=offscreen'])


dap_parser_test_src = [
  'tests/dap/test_dap_parser.cpp',
  'src/core/hpc/dap/DapCoordinator.cpp',
  'src/core/hpc/memory/NativeMemoryReader.cpp',
  'src/core/managers/ConfigManager.cpp', 'src/core/managers/ConfigManagerToml.cpp', 'src/core/managers/ConfigManagerWorkspace.cpp'
]

dap_parser_test_moc_headers = [
  'tests/dap/test_dap_parser.hpp',
  'src/core/hpc/dap/DapCoordinator.hpp',
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
  dependencies : [qt6_dep, tomlplusplus_dep],
  build_by_default : false
)

test('dap_parser_tests', gridlock_dap_parser_tests, env: ['QT_QPA_PLATFORM=offscreen'])

dap_commands_test_src = [
  'tests/dap/test_dap_commands.cpp',
  'src/core/hpc/dap/DapCoordinator.cpp',
  'src/core/hpc/memory/NativeMemoryReader.cpp',
  'src/core/hpc/gdb/GdbRankCoordinator.cpp', 'src/core/hpc/gdb/GdbMiParser.cpp',
  'src/core/hpc/gdb/GdbRankInspection.cpp', 'src/core/hpc/gdb/GdbRankCommands.cpp',
  'src/core/commands/DebugCommands.cpp',
  'src/core/managers/ConfigManager.cpp', 'src/core/managers/ConfigManagerToml.cpp', 'src/core/managers/ConfigManagerWorkspace.cpp',
  'src/ui/views/SourceCodeView.cpp', 'src/ui/views/SourceCodeViewInteraction.cpp',
  'src/ui/views/EditorSyntaxHighlighter.cpp',
  'src/ui/widgets/InlineFindBar.cpp',
  'src/ui/dialogs/debugger/ConditionalBreakpointDialog.cpp', 'src/core/utils/ClangdLspClient.cpp', 'src/core/utils/ClangdLspNavigation.cpp'
]

dap_commands_test_moc_headers = [
  'tests/dap/test_dap_commands.hpp',
  'src/core/hpc/dap/DapCoordinator.hpp',
  'src/core/hpc/gdb/GdbRankCoordinator.hpp',
  'src/core/hpc/IBackendCoordinator.hpp',
  'src/ui/views/SourceCodeView.hpp',
  'src/ui/views/EditorSyntaxHighlighter.hpp',
  'src/ui/widgets/InlineFindBar.hpp',
  'src/ui/dialogs/debugger/ConditionalBreakpointDialog.hpp', 'src/core/utils/ClangdLspClient.hpp'
]

processed_dap_commands_test_files = qt6.compile_moc(
  headers : dap_commands_test_moc_headers,
  dependencies: qt6_dep
)

gridlock_dap_commands_tests = executable('gridlock_dap_commands_tests',
  dap_commands_test_src,
  processed_dap_commands_test_files,
  include_directories : inc,
  dependencies : [qt6_dep, tomlplusplus_dep],
  build_by_default : false
)

test('dap_commands_tests', gridlock_dap_commands_tests, env: ['QT_QPA_PLATFORM=offscreen'])

dap_breakpoints_test_src = [
  'tests/dap/test_dap_breakpoints.cpp',
  'src/core/hpc/dap/DapCoordinator.cpp',
  'src/core/hpc/memory/NativeMemoryReader.cpp',
  'src/core/managers/ConfigManager.cpp', 'src/core/managers/ConfigManagerToml.cpp', 'src/core/managers/ConfigManagerWorkspace.cpp'
]

dap_breakpoints_test_moc_headers = [
  'tests/dap/test_dap_breakpoints.hpp',
  'src/core/hpc/dap/DapCoordinator.hpp',
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
  dependencies : [qt6_dep, tomlplusplus_dep],
  build_by_default : false
)

test('dap_breakpoints_tests', gridlock_dap_breakpoints_tests, env: ['QT_QPA_PLATFORM=offscreen'])

dap_stacktrace_test_src = [
  'tests/dap/test_dap_stacktrace.cpp',
  'src/core/hpc/dap/DapCoordinator.cpp',
  'src/core/hpc/memory/NativeMemoryReader.cpp',
  'src/core/managers/ConfigManager.cpp', 'src/core/managers/ConfigManagerToml.cpp', 'src/core/managers/ConfigManagerWorkspace.cpp'
]

dap_stacktrace_test_moc_headers = [
  'tests/dap/test_dap_stacktrace.hpp',
  'src/core/hpc/dap/DapCoordinator.hpp',
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
  dependencies : [qt6_dep, tomlplusplus_dep],
  build_by_default : false
)

test('dap_stacktrace_tests', gridlock_dap_stacktrace_tests, env: ['QT_QPA_PLATFORM=offscreen'])

dap_variables_test_src = [
  'tests/dap/test_dap_variables.cpp',
  'src/core/hpc/dap/DapCoordinator.cpp',
  'src/core/hpc/memory/NativeMemoryReader.cpp',
  'src/core/hpc/gdb/GdbRankCoordinator.cpp', 'src/core/hpc/gdb/GdbMiParser.cpp',
  'src/core/hpc/gdb/GdbRankInspection.cpp', 'src/core/hpc/gdb/GdbRankCommands.cpp',
  'src/core/managers/ConfigManager.cpp', 'src/core/managers/ConfigManagerToml.cpp', 'src/core/managers/ConfigManagerWorkspace.cpp',
  'src/core/models/VariableTreeModel.cpp'
]

dap_variables_test_moc_headers = [
  'tests/dap/test_dap_variables.hpp',
  'src/core/hpc/dap/DapCoordinator.hpp',
  'src/core/hpc/gdb/GdbRankCoordinator.hpp',
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
  dependencies : [qt6_dep, tomlplusplus_dep],
  build_by_default : false
)

test('dap_variables_tests', gridlock_dap_variables_tests, env: ['QT_QPA_PLATFORM=offscreen'])

dap_evaluate_test_src = [
  'tests/dap/test_dap_evaluate.cpp',
  'src/core/hpc/dap/DapCoordinator.cpp',
  'src/core/hpc/memory/NativeMemoryReader.cpp',
  'src/core/managers/ConfigManager.cpp', 'src/core/managers/ConfigManagerToml.cpp', 'src/core/managers/ConfigManagerWorkspace.cpp'
]

dap_evaluate_test_moc_headers = [
  'tests/dap/test_dap_evaluate.hpp',
  'src/core/hpc/dap/DapCoordinator.hpp',
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
  dependencies : [qt6_dep, tomlplusplus_dep],
  build_by_default : false
)

test('dap_evaluate_tests', gridlock_dap_evaluate_tests, env: ['QT_QPA_PLATFORM=offscreen'])

dap_lifecycle_test_src = [
  'tests/dap/test_dap_lifecycle.cpp',
  'src/core/hpc/dap/DapCoordinator.cpp',
  'src/core/hpc/memory/NativeMemoryReader.cpp',
  'src/core/managers/ConfigManager.cpp', 'src/core/managers/ConfigManagerToml.cpp', 'src/core/managers/ConfigManagerWorkspace.cpp'
]

dap_lifecycle_test_moc_headers = [
  'tests/dap/test_dap_lifecycle.hpp',
  'src/core/hpc/dap/DapCoordinator.hpp',
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
  dependencies : [qt6_dep, tomlplusplus_dep],
  build_by_default : false
)

test('dap_lifecycle_tests', gridlock_dap_lifecycle_tests, env: ['QT_QPA_PLATFORM=offscreen'])

dap_memory_test_src = [
  'tests/dap/test_dap_memory.cpp',
  'src/core/hpc/dap/DapCoordinator.cpp',
  'src/core/hpc/memory/NativeMemoryReader.cpp',
  'src/core/managers/ConfigManager.cpp', 'src/core/managers/ConfigManagerToml.cpp', 'src/core/managers/ConfigManagerWorkspace.cpp'
]

dap_memory_test_moc_headers = [
  'tests/dap/test_dap_memory.hpp',
  'src/core/hpc/dap/DapCoordinator.hpp',
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
  dependencies : [qt6_dep, tomlplusplus_dep],
  build_by_default : false
)

test('dap_memory_tests', gridlock_dap_memory_tests, env: ['QT_QPA_PLATFORM=offscreen'])

hpc_memory_test_src = [
  'tests/hpc/test_hpc_memory.cpp',
  'src/core/hpc/memory/NativeMemoryReader.cpp'
]

hpc_memory_test_moc_headers = [
  'tests/hpc/test_hpc_memory.hpp'
]

processed_hpc_memory_test_files = qt6.compile_moc(
  headers : hpc_memory_test_moc_headers,
  dependencies: qt6_dep
)

gridlock_hpc_memory_tests = executable('gridlock_hpc_memory_tests',
  hpc_memory_test_src,
  processed_hpc_memory_test_files,
  include_directories : inc,
  dependencies : [qt6_dep, tomlplusplus_dep],
  build_by_default : false
)

test('hpc_memory_tests', gridlock_hpc_memory_tests, env: ['QT_QPA_PLATFORM=offscreen'])

stride_security_test_src = [
  'tests/core/test_stride_security.cpp',
  'src/core/hpc/memory/MemoryBoundsValidator.cpp'
]

stride_security_test_moc_headers = [
  'tests/core/test_stride_security.hpp'
]

processed_stride_security_test_files = qt6.compile_moc(
  headers : stride_security_test_moc_headers,
  dependencies: qt6_dep
)

gridlock_stride_security_tests = executable('gridlock_stride_security_tests',
  stride_security_test_src,
  processed_stride_security_test_files,
  include_directories : inc,
  dependencies : [qt6_dep, tomlplusplus_dep],
  build_by_default : false
)

test('stride_security_tests', gridlock_stride_security_tests, env: ['QT_QPA_PLATFORM=offscreen'])

dap_native_bridge_test_src = [
  'tests/dap/test_dap_native_bridge.cpp',
  'src/core/hpc/dap/DapCoordinator.cpp',
  'src/core/hpc/memory/NativeMemoryReader.cpp',
  'src/core/managers/ConfigManager.cpp', 'src/core/managers/ConfigManagerToml.cpp', 'src/core/managers/ConfigManagerWorkspace.cpp'
]

dap_native_bridge_test_moc_headers = [
  'tests/dap/test_dap_native_bridge.hpp',
  'src/core/hpc/dap/DapCoordinator.hpp',
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
  dependencies : [qt6_dep, tomlplusplus_dep],
  build_by_default : false
)

test('dap_native_bridge_tests', gridlock_dap_native_bridge_tests, env: ['QT_QPA_PLATFORM=offscreen'])

sanitizer_test_src = [
  'tests/hpc/test_sanitizer.cpp',
  'src/core/hpc/gdb/MiSanitizer.cpp'
]

sanitizer_test_moc_headers = [
  'tests/hpc/test_sanitizer.hpp'
]

processed_sanitizer_test_files = qt6.compile_moc(
  headers : sanitizer_test_moc_headers,
  dependencies: qt6_dep
)

gridlock_sanitizer_tests = executable('gridlock_sanitizer_tests',
  sanitizer_test_src,
  processed_sanitizer_test_files,
  include_directories : inc,
  dependencies : [qt6_dep, tomlplusplus_dep],
  build_by_default : false
)

test('sanitizer_tests', gridlock_sanitizer_tests, env: ['QT_QPA_PLATFORM=offscreen'])

obfuscator_test_src = [
  'tests/hpc/test_obfuscator.cpp',
  'src/core/hpc/system/TelemetryObfuscator.cpp'
]

obfuscator_test_moc_headers = [
  'tests/hpc/test_obfuscator.hpp',
  'src/core/hpc/system/TelemetryObfuscator.hpp'
]

processed_obfuscator_test_files = qt6.compile_moc(
  headers : obfuscator_test_moc_headers,
  dependencies: qt6_dep
)

gridlock_obfuscator_tests = executable('gridlock_obfuscator_tests',
  obfuscator_test_src,
  processed_obfuscator_test_files,
  include_directories : inc,
  dependencies : [qt6_dep, tomlplusplus_dep],
  build_by_default : false
)

test('obfuscator_tests', gridlock_obfuscator_tests, env: ['QT_QPA_PLATFORM=offscreen'])

advanced_features_test_src = [
  'tests/core/test_advanced_features.cpp',
  'src/core/hpc/analysis/DeadlockAnalyzer.cpp',
  'src/core/hpc/gdb/GdbRankCoordinator.cpp', 'src/core/hpc/gdb/GdbMiParser.cpp',
  'src/core/hpc/gdb/GdbRankInspection.cpp', 'src/core/hpc/gdb/GdbRankCommands.cpp',
  'src/core/models/VariableTreeModel.cpp',
  'src/core/hpc/dap/DapCoordinator.cpp',
  'src/core/hpc/memory/NativeMemoryReader.cpp',
  'src/core/managers/ConfigManager.cpp', 'src/core/managers/ConfigManagerToml.cpp', 'src/core/managers/ConfigManagerWorkspace.cpp'
]

advanced_features_test_moc_headers = [
  'tests/core/test_advanced_features.hpp',
  'src/core/hpc/analysis/DeadlockAnalyzer.hpp',
  'src/core/hpc/gdb/GdbRankCoordinator.hpp',
  'src/core/hpc/dap/DapCoordinator.hpp',
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
  dependencies : [qt6_dep, tomlplusplus_dep],
  build_by_default : false
)

test('advanced_features_tests', gridlock_advanced_features_tests, env: ['QT_QPA_PLATFORM=offscreen'])

memory_export_test_src = [
  'tests/hpc/test_memory_export.cpp',
  'src/core/hpc/memory/MemoryExporter.cpp'
]

memory_export_test_moc_headers = [
  'tests/hpc/test_memory_export.hpp'
]

processed_memory_export_test_files = qt6.compile_moc(
  headers : memory_export_test_moc_headers,
  dependencies: qt6_dep
)

gridlock_memory_export_tests = executable('gridlock_memory_export_tests',
  memory_export_test_src,
  processed_memory_export_test_files,
  include_directories : inc,
  dependencies : [qt6_dep, tomlplusplus_dep],
  build_by_default : false
)

test('memory_export_tests', gridlock_memory_export_tests, env: ['QT_QPA_PLATFORM=offscreen'])

mi_parser_test_src = [
  'tests/hpc/test_mi_parser.cpp',
  'src/ui/GdbConsoleWidget.cpp',
  'src/ui/widgets/InlineFindBar.cpp'
]

mi_parser_test_moc_headers = [
  'src/ui/GdbConsoleWidget.hpp',
  'src/ui/widgets/InlineFindBar.hpp'
]

processed_mi_parser_test_files = qt6.compile_moc(
  headers : mi_parser_test_moc_headers,
  sources : 'tests/hpc/test_mi_parser.cpp',
  dependencies: qt6_dep
)

gridlock_mi_parser_tests = executable('gridlock_mi_parser_tests',
  mi_parser_test_src,
  processed_mi_parser_test_files,
  include_directories : inc,
  dependencies : [qt6_dep, tomlplusplus_dep],
  build_by_default : false
)

test('mi_parser_tests', gridlock_mi_parser_tests, env: ['QT_QPA_PLATFORM=offscreen'])

cliparser_test_src = [
  'tests/core/test_cliparser.cpp',
  'src/core/utils/CliParser.cpp',
  'src/core/Version.cpp'
]

processed_cliparser_test_files = qt6.compile_moc(
  sources : 'tests/core/test_cliparser.cpp',
  dependencies: qt6_dep
)

gridlock_cliparser_tests = executable('gridlock_cliparser_tests',
  cliparser_test_src,
  processed_cliparser_test_files,
  include_directories : inc,
  dependencies : [qt6_dep],
  build_by_default : false
)

test('cliparser_tests', gridlock_cliparser_tests, env: ['QT_QPA_PLATFORM=offscreen'])



custom_target('mpi_mm_bin',
  input: 'tutorial/mpi_mm.c',
  output: 'mpi_mm',
  command: ['mpicc', '-g', '-O0', '@INPUT@', '-o', '@OUTPUT@'],
  build_by_default: true
)

custom_target('catchpoint_exceptions_bin',
  input: 'tutorial/catchpoint_exceptions.cpp',
  output: 'catchpoint_exceptions',
  command: ['mpicxx', '-g', '-O0', '@INPUT@', '-o', '@OUTPUT@'],
  build_by_default: true
)

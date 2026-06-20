project('GridLock', 'cpp',
  version : '0.1.0',
  default_options : ['warning_level=3', 'cpp_std=c++23'])

# Dependencies
qt6 = import('qt6')
qt6_dep = dependency('qt6', modules: ['Core', 'Widgets', 'Gui', 'Test'])

# Source files
src_files = [
  'src/main.cpp',
  'src/GridLockApp.cpp',
  'src/GdbRankCoordinator.cpp',
  'src/ui/MainWindow.cpp',
  'src/ui/ServerRackView.cpp',
  'src/ui/SourceCodeView.cpp',
  'src/ui/DisassemblyView.cpp',
  'src/ui/ReferenceDock.cpp',
  'src/ui/DifferentialGrid.cpp',
  'src/ui/TerminalDock.cpp',
  'src/backend/TestGenerator.cpp',
  'src/backend/GridLockAutomationRunner.cpp'
]

# Headers for MOC (Meta-Object Compiler)
moc_headers = [
  'src/GridLockApp.hpp',
  'src/GdbRankCoordinator.hpp',
  'src/ui/MainWindow.hpp',
  'src/ui/ServerRackView.hpp',
  'src/ui/SourceCodeView.hpp',
  'src/ui/DisassemblyView.hpp',
  'src/ui/ReferenceDock.hpp',
  'src/ui/DifferentialGrid.hpp',
  'src/ui/TerminalDock.hpp',
  'src/backend/GridLockAutomationRunner.hpp'
]

# Process Qt6 files
processed_qt_files = qt6.compile_moc(
  headers : moc_headers,
  dependencies: qt6_dep
)

# Include directories
inc = include_directories('src')

# Executable
executable('gridlock',
  src_files,
  processed_qt_files,
  include_directories : inc,
  dependencies : qt6_dep,
  install : true)

# Test executable
test_src = [
  'tests/test_coordinator.cpp',
  'src/GdbRankCoordinator.cpp'
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
  dependencies : qt6_dep
)

test('coordinator_tests', gridlock_tests)
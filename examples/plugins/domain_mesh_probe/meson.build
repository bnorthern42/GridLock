project(
  'domain_mesh_probe',
  'cpp',
  version: '1.0.0',
  default_options: [
    'cpp_std=c++23',
    'warning_level=3',
    'werror=true',
  ],
)

gridlock_inc = include_directories('../../../include')

shared_library(
  'domain_mesh_probe',
  'domain_mesh_probe.cpp',
  include_directories: gridlock_inc,
  gnu_symbol_visibility: 'hidden',
  install: false,
)

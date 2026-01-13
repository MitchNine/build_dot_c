# Change Log

All notable changes to this project will be documented in this file.

## [1.1.0] - 2026-01-14

### Added

- Proper multi-threading and job support.
- New section for lib include paths (__lib_incs__).
- Allow for cpp and c code to be compiled into the same project.
  - This comes with the caveat that the executible needs to be compiled with
    a c++ compiler if there is any c++ files compiled into the project since the
    linker needs to deal with c++ linking.
- Adds in new print function for debugging

## [1.0.0] - 2025-08-13

### Added

- Refacted the codebase to be more readable
- A first pass implementation of multi-threading
- Uses global structures to define the settings of the build

## [0.8.3] - 2025-05-28

### Added

- Version numbering into the project
- Fixed some issues with buffer sizes of flags
- New helper functions for file path manipulation
- Added const where needed
- New readme.md for helping with using the build.c file

### Removed

- All pre and post build commands
  - __pre_build__
  - __post_build__
  - __pre_run__
  - __post_run__

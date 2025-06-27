# build_dot_c

A self-hosting build system for C/C++ projects, configured entirely in a single `build.c` file.  
The build executable will automatically rebuild itself when changes are detected in `build.c`.

## Features

- Simple, single-file build configuration (`build.c`)
- Supports debug and release builds
- Automatic dependency tracking via `-MD`
- Cleans output directory
- Passes arguments to the built executable
- Self-rebuilding when `build.c` changes

## Usage

```sh
./build [dev|rel|clean|build-only|version|help] -- [ARGS...]
```

### Commands

- `dev`         : Build the target executable with `-DDEBUG` and debug info
- `rel`         : Build the target executable with `-DRELEASE` and optimizations
- `clean`       : Remove the output directory
- `build-only`  : Only build the build executable, not the target
- `version`     : Print the build system version
- `help`        : Show help text
- `--`          : Run the built executable, passing any arguments after `--` to it

### Example

```sh
./build dev -- --input=foo.txt
./build rel
./build clean
```

## Configuration

Edit the top of `build.c` to set:

- `cc`        : Compiler for target (default: `clang`)
- `c_exe`     : Name of the target executable
- `c_src[]`   : List of source files to compile
- `c_cflags[]`: Compiler flags
- `c_clibs[]` : Libraries to link
- `build_cc`  : Compiler for `build.c`
- `build_c`   : Path to `build.c`
- `build_dir` : Output directory
- `build_exe` : Name of the build executable
- `build_ver` : Version string

## How it works

- On each invocation, `build` checks if `build.c` or the build mode has changed.
- If so, it rebuilds itself, then re-invokes with the same arguments.
- Otherwise, it checks dependencies and only recompiles changed files.
- Output and intermediate files are placed in the directory specified by `build_dir`.

## License

MIT License. See top of `build.c` for details.

<img width="454" height="150" alt="Build Dot C" src="https://github.com/user-attachments/assets/c42238d4-0d0c-48c9-9be1-299c32ae9447" />


A self-hosting build system for C/C++ projects, configured entirely in a single `build.c` file.  
The build executable will automatically rebuild itself when changes are detected in `build.c`.

## Features

- Simple, single-file build configuration (`build.c`)
- Supports debug and release builds
- Automatic dependency tracking via `-MD`
- Cleans output directory
- Passes arguments to the built executable
- Self-rebuilding when `build.c` changes
- Optional multithreaded compilation

## Usage

```sh
./build [dbg|rel|clean|no-threading|build-only|j [NUM]|version|help] -- [ARGS...]
```

### Commands

- `dbg`           : Build the target executable with `-DDEBUG` and debug info
- `rel`           : Build the target executable with `-DRELEASE` and optimizations
- `clean`         : Remove the output directory
- `no-threading`  : Disable multithreaded compilation
- `build-only`    : Only build the build executable, not the target
- `j [NUM]`       : Sets the number of threads to use for building source files\n"
- `version`       : Print the build system version
- `help`          : Show help text
- `--`            : Run the built executable, passing any arguments after `--` to it

### Example

```sh
./build dbg -- --input=foo.txt
./build rel
./build rel j64
./build clean
./build no-threading
```

## Configuration

Edit the top of `build.c` to set:

- `cc.c`       : C Compiler for target (default: `"gcc"`)
- `cc.cpp`     : C++ Compiler for target (default: `"g++"`)
- `exe`        : Name of the target executable
- `dir`        : Output directory
- `src[]`      : List of source files to compile
- `flags[]`    : Compiler flags
- `incs[]`     : Directories to include
- `lib_incs[]` : Directories to include for linking to libraries
- `libs[]`     : Libraries to link
- `build.cc`   : Compiler for `build.c`
- `build.file` : Path to `build.c`
- `build.exe`  : Name of the build executable
- `build.ver`  : Version string

## How it works

- On each invocation, `build` checks if `build.c` or the build mode has changed.
- If so, it rebuilds itself, then re-invokes with the same arguments.
- Otherwise, it checks dependencies and only recompiles changed files.
- Output and intermediate files are placed in the directory specified by `dir`.
- Uses a lock file to track build state and avoid concurrent builds.

## License

MIT License. See top of `build.c` or the LICENCE.md file for details.

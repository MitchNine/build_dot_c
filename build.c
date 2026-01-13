/******************************************************************************
 * MIT License
 *
 * Copyright (c) 2025 Mitchell Jenkins
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *****************************************************************************
 *
 * Builds C/C++ target applications using the configuration provided in the
 * build.c file. The build executable will rebuild itself when changes are
 * detected within the build.c file.
 *****************************************************************************/

#include <stdlib.h>

static const int MAX_CPU_CORES = 8;
typedef struct Compilers {
    const char *c;
    const char *cpp;
} Compilers;
struct Config {
    const Compilers cc;          // Compiler to use for building target and build.c
    const char *exe;             // Target executable name
    const char *dir;             // The output directory that the target executable will end up
    const char *const *src;      // List of all the .c files to be compiled
    const char *const *flags;    // List of all the flags that will be sent to the compiler
    const char *const *incs;     // List of libraries to link against
    const char *const *lib_incs; // List of libraries to link against
    const char *const *libs;     // List of libraries to link against
} c_config = {
    .cc = (Compilers){ .c = "gcc", .cpp = "g++" },
    .exe = "example_app",
    .dir = "./out",

    .src = (const char *[]) {
        "./src/main.c",
        NULL, // Sentinel to mark the end of the array
    },

    .flags = (const char *[]) {
        "-MD",
        "-Wall",
#ifdef DEBUG
        "-fsanitize=address",
        "-O0",
        "-DDEBUG",
        "-g",
#endif
        NULL, // Sentinel to mark the end of the array
    },

    .incs = (const char *[]) {
        NULL, // Sentinel to mark the end of the array
    },

    .lib_incs = (const char *[]) {
        NULL, // Sentinel to mark the end of the array
    },

    .libs = (const char *[]) {
        NULL, // Sentinel to mark the end of the array
    },
};
typedef struct Config config_t;

struct Build {
    const char *cc;    // Compiler to use for building target and build.c
    const char *file;  // Target executable name
    const char *exe;   // The name of the executable the build.c file will compile into
    const char *ver;   // Version of the build.c
} build = { "clang", "./build.c", "./build", "1.1.0" };
typedef struct Build build_t;

/*****************************************************************************
 *****************************************************************************/
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <libgen.h>
#include <linux/limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>
#include <pthread.h>
#include <stdatomic.h>

static int g_thread_count = 0;
static pthread_mutex_t g_thread_count_mutex;
static pthread_mutex_t g_thread_print_mutex;
typedef enum BuildMode {
    MODE_NONE,
    MODE_DEV,
    MODE_REL
} BuildMode;

struct LockFile {
    bool lock;           // Indicates if the build is locked
    bool rebuilding;     // Indicates if the build file has been modified
    long last_build;     // Timestamp of the last build
    BuildMode last_mode; // Last build mode used
} lockfile = { false, 0, MODE_NONE };

typedef struct InternalConfig {
    bool run;         // Indicates if the build should run after building
    bool clean;       // Indicates if the build directory should be cleaned before building
    bool build_only;  // Indicates if only the build file should be built without running it
    int thread_count; // Indicates if the build should be done in a threaded manner
    int run_argc;     // Number of arguments to pass to the build file when running it
    BuildMode mode;   // Build mode to use (none, development, or release)
} InternalConfig;

void print_help() {
    printf("\n"
"██████╗ ██╗   ██╗██╗██╗     ██████╗     ██████╗\n"
"██╔══██╗██║   ██║██║██║     ██╔══██╗   ██╔════╝\n"
"██████╔╝██║   ██║██║██║     ██║  ██║   ██║     \n"
"██╔══██╗██║   ██║██║██║     ██║  ██║   ██║     \n"
"██████╔╝╚██████╔╝██║███████╗██████╔╝██╗╚██████╗\n"
"╚═════╝  ╚═════╝ ╚═╝╚══════╝╚═════╝ ╚═╝ ╚═════╝\n"
"version %s\n\n"
"Usage: ./build [dbg|rel|clean|build-only|version|help] -- [ARGS]...\n"
"Builds C/C++ target applications using the configuration provided in the\n"
"build.c file. The build executable will rebuild itself when changes are\n"
"detected within the build.c file.\n\n"
"Command options:\n"
"    dbg            Build the target executable with -DDEBUG enabled\n"
"    rel            Build the target executable with -DRELEASE enabled\n"
"    clean          Removes the output directory\n"
"    build-only     Only builds the build executable not the target executable\n"
"    version        Displays the version of the build.c\n"
"    help           Displays this text\n"
"    --             Runs the executable and all args after the double dashes\n"
"                   will be passed onto the executable.\n\n"
" Example:\n"
"    ./build dev -- --file=./output/\n", build.ver);
}

static inline void print(const char *section, const char *color, const char *fmt, ...) {
    va_list va;
    va_start(va, fmt);
    pthread_mutex_lock(&g_thread_print_mutex);
    if (isatty(1) == 1) {
        printf("[\033[%sm%s\033[0m] ", color, section);
    } else {
        printf("[%s] ", section);
    }
    vprintf(fmt, va);
    pthread_mutex_unlock(&g_thread_print_mutex);
    va_end(va);
}

// Utility functions
int exec(const char *format, ...)
{ // {{{
    char cmd[PATH_MAX];
    va_list val;
    va_start(val, format);
    vsprintf(cmd, format, val);
    va_end(val);
    print("LOAD", "34", "%s\n", cmd);
    fflush(stdout);
    int status = system(cmd);
    print("DONE", "32", "%s\n", cmd);
    fflush(stdout);
    return WEXITSTATUS(status);
} // }}}
int recursive_mkdir(const char *dir)
{ // {{{
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", dir);
    for (char *p = tmp + 1; *p; p++) if (*p == '/') { *p = 0; mkdir(tmp, S_IRWXU); *p = '/'; }
    return mkdir(tmp, S_IRWXU);
} // }}}
void append_strings(char *cmd, const char *const *flags)
{ // {{{
    for (int i = 0;; i++) {
        if (flags[i] == NULL) break; // Sentinel check for end of array
        snprintf(cmd + strlen(cmd), PATH_MAX - strlen(cmd), "%s ", flags[i]);
    }
} // }}}
void strip_extension(const char *src, char *dst, size_t dst_size)
{ // {{{
    snprintf(dst, dst_size, "%s", src);
    char *dot = strrchr(dst, '.');
    if (dot) *dot = '\0';
} // }}}
unsigned int get_array_length(const char *const *array)
{ // {{{
    unsigned int length = 0;
    while (array[length] != NULL) {
        length++;
    }
    return length;
} // }}}

// Dependency functions
__time_t get_file_modified_time(const char *file_path)
{ // {{{
    struct stat file_stat;
    if (stat(file_path, &file_stat) == -1) {
        perror("stat");
        return -1;
    }
    return file_stat.st_mtime;
} // }}}
__time_t last_dependencies_modified(const char *file_path)
{ // {{{
    FILE *fp = fopen(file_path, "r");
    if (fp == NULL) {
        fprintf(stderr, "Error: Failed to open file %s for reading\n", file_path);
        return -1;
    }
    __time_t last_modified = 0;

    char* line = NULL;
    size_t line_len = 0;
    while (getline(&line, &line_len, fp) != -1) {
        char *tok = strtok(line, " ");
        while (tok != NULL) {
            size_t len = strlen(tok);
            if (tok[len - 1] == '\n') tok[--len] = '\0';
            if (access(tok, F_OK) == 0) {
                struct stat file_stat;
                if (stat(tok, &file_stat) == -1) {
                    fprintf(stderr, "Error: Failed to stat file %s\n", tok);
                    continue;
                }
                if (file_stat.st_mtime > last_modified) {
                    last_modified = file_stat.st_mtime;
                }
            }
            tok = strtok(NULL, " ");
        }
    }

    fclose(fp);
    return last_modified;
} // }}}

bool serialize_lock_file(const char *file_path, const struct LockFile *lock)
{ // {{{
    FILE *fp = fopen(file_path, "w+");
    if (fp == NULL) {
        fprintf(stderr, "Error: Failed to open lock file %s for writing\n", file_path);
        return false;
    }
    int written = fwrite(lock, sizeof(struct LockFile), 1, fp);
    if (written <= 0) {
        fprintf(stderr, "Error: Failed to write lock file %s\n", file_path);
        fprintf(stderr, "%s\n", strerror(errno));
    }
    fclose(fp);
    return written > 0;
} // }}}
bool deserialize_lock_file(const char *file_path, struct LockFile *lock)
{ // {{{
    FILE *fp = fopen(file_path, "r");
    if (fp == NULL) {
        // TODO: Ignore filed to open lock file when no output directory is available
        // fprintf(stderr, "Error: Failed to open lock file %s for reading\n", file_path);
        // fprintf(stderr, "%s\n", strerror(errno));
        return false;
    }
    int read = fread(lock, sizeof(struct LockFile), 1, fp);
    if (read <= 0) {
        fprintf(stderr, "Error: Failed to read lock file %s\n", file_path);
        fprintf(stderr, "%s\n", strerror(errno));
    }
    fclose(fp);
    return read > 0;
} // }}}

// Path manipulation functions
void get_path_without_filename(const char *src, char *dst, size_t dst_size)
{ // {{{
    snprintf(dst, dst_size, "%s", src);
    char *slash = strrchr(dst, '/');
    if (slash) {
        *slash = '\0';
    } else {
        dst[0] = '\0';
    }
} // }}}
void get_filename_without_path(const char *src, char *dst, size_t dst_size)
{ // {{{
    const char *slash = strrchr(src, '/');
    if (slash) {
        snprintf(dst, dst_size, "%s", slash + 1);
    } else {
        snprintf(dst, dst_size, "%s", src);
    }
    dst[dst_size - 1] = '\0';
} // }}}

// Build functions
void *build_file(const void *arg)
{ // {{{
    const char *cmd = (const char *)arg;
    fflush(stdout);
    int status = exec("%s", cmd);
    if (status != 0) {
        fprintf(stderr, "Error: build_file failed with status %d\n", status);
    }
    pthread_mutex_lock(&g_thread_count_mutex);
    g_thread_count = g_thread_count - 1;
    pthread_mutex_unlock(&g_thread_count_mutex);
    return NULL;
} // }}}
int make_targets(const config_t *config, char* build_file_cmd[], unsigned int size)
{ // {{{
    for (unsigned int i = 0; i < size; i++) {
        if (build_file_cmd[i] == NULL) {
            fprintf(stderr, "Error: build_file_cmd[%u] is NULL\n", i);
            return -1;
        }

        char* const cmd = build_file_cmd[i];
        char dir[PATH_MAX], filename[PATH_MAX];

        // Strip the extension and get the directory and filename
        strip_extension(config->src[i], filename, PATH_MAX);
        get_filename_without_path(filename, filename, sizeof(filename));
        get_path_without_filename(config->src[i], dir, sizeof(dir));

        // Create the output directory if it doesn't exist
        char full_dir[PATH_MAX];
        snprintf(full_dir, PATH_MAX - strlen(full_dir) - 2, "%s/%s", config->dir, dir);
        recursive_mkdir(full_dir);

        // Check if the source file has been modified since the last build
        const __time_t last_modified = get_file_modified_time(config->src[i]);

        // Check the .d file for dependencies
        char dep_file[PATH_MAX];
        snprintf(dep_file, PATH_MAX - strlen(dep_file) - 2, "%s/%s.d", full_dir, filename);
        if (access(dep_file, F_OK) == 0) {
            // If the .d file exists, check its dependencies
            __time_t deps_last_modified  = last_dependencies_modified(dep_file);

            // If the dependencies and source file have not changed since the last build,
            // skip building this file
            if (deps_last_modified < lockfile.last_build && last_modified < lockfile.last_build) {
                build_file_cmd[i][0] = '\0';
                continue;
            }
        }

        // Create the command to compile the source file
        const char *extension = strrchr(config->src[i], '.');
        if (extension != NULL) {
            if (strstr(extension, "cpp") == NULL) {
                snprintf(cmd, PATH_MAX - strlen(cmd), "%s -c %s -o %s/%s.o ",
                        config->cc.c, config->src[i], full_dir, filename);
            } else {
                snprintf(cmd, PATH_MAX - strlen(cmd), "%s -c %s -o %s/%s.o ",
                        config->cc.cpp, config->src[i], full_dir, filename);
            }
        } else {
            snprintf(cmd, PATH_MAX - strlen(cmd), "%s -c %s -o %s/%s.o ",
                    config->cc.c, config->src[i], full_dir, filename);
        }
        append_strings(cmd, config->flags);
        append_strings(cmd, config->incs);
    }
    return 0;
} // }}}
int make_executable(const config_t *config, char* build_exe_cmd)
{ // {{{
    if (config->exe == NULL) {
        fprintf(stderr, "Error: config->exe is NULL\n");
        return -1;
    }
    char* const cmd = build_exe_cmd;
    snprintf(cmd, PATH_MAX, "%s -o %s/%s ", config->cc.cpp, config->dir, config->exe);
    for (unsigned int i = 0; i < get_array_length(config->src); i++) {
        if (config->src[i] == NULL) {
            fprintf(stderr, "Error: config->src[%u] is NULL\n", i);
            return -1;
        }
        char filename[PATH_MAX];
        strip_extension(config->src[i], filename, sizeof(filename));
        snprintf(cmd + strlen(cmd), PATH_MAX - strlen(cmd), "%s/%s.o ", config->dir, filename);
    }
    append_strings(cmd, config->flags);
    append_strings(cmd, config->incs);
    append_strings(cmd, config->lib_incs);
    append_strings(cmd, config->libs);
    return 0;
} // }}}
int make_build(const BuildMode mode, char* cmd)
{ // {{{
    if (build.cc == NULL || build.file == NULL || build.exe == NULL) {
        fprintf(stderr, "Error: Build configuration is incomplete\n");
        return -1;
    }
    snprintf(cmd, PATH_MAX, "%s -o %s %s ", build.cc, build.exe, build.file);
    switch (mode) {
        case MODE_REL:
            append_strings(cmd, (const char *[]){"-O2", "-lpthread", NULL});
            break;
        case MODE_DEV:
            append_strings(cmd, (const char *[]){"-Wall", "-DDEBUG", "-g", "-lpthread", NULL});
            break;
        case MODE_NONE:
            append_strings(cmd, (const char *[]){"-Wall", "-lpthread", NULL});
            break;
        default:
            fprintf(stderr, "Error: Invalid build mode\n");
            return -1;
    }
    return 0;
} // }}}

// Compile function
int compile_build_file(const char *lock_file_path, int argc, char const *const argv[], const InternalConfig *conf)
{ // {{{
    __time_t build_file_last_modified = get_file_modified_time(build.file);

    // If the build file has been modified, rebuild it
    if (build_file_last_modified >= lockfile.last_build
            || lockfile.last_mode != conf->mode) {

        if (lockfile.rebuilding) {
            return 0; // Already rebuilding, no need to rebuild again
        }

        // Rebuild the build file
        char build_file_cmd[PATH_MAX];
        if (make_build(conf->mode, build_file_cmd) != 0) {
            fprintf(stderr, "Error: make_build failed\n");
            return -1;
        }

        if (exec("%s", build_file_cmd) != 0) {
            fprintf(stderr, "Error: Failed to build the build file\n");
            return -1;
        }

        if (lockfile.last_mode != conf->mode) {
            lockfile.last_build = 0; // Reset last build time if the mode has changed
        } else {
            lockfile.last_build = time(NULL);
        }
        lockfile.lock = false;
        lockfile.last_mode = conf->mode;
        lockfile.rebuilding = true; // Mark the build file as dirty since it has been rebuilt
        serialize_lock_file(lock_file_path, &lockfile);

        char cmd[PATH_MAX];
        snprintf(cmd, PATH_MAX, "%s", build.exe);
        for (int i = 1; i < argc; i++) {
            strcat(cmd, " ");
            strcat(cmd, argv[i]);
        }
        if (exec("%s", cmd) != 0) {
            fprintf(stderr, "Error: Failed to run the build file\n");
            return -1;
        }
        exit(0); // Exit after running the build file
    }

    return 0; // No need to rebuild the build file
} // }}}
int compile_files(const config_t *config, const InternalConfig *internal_config)
{ // {{{
    (void)config; // Mark as intentionally unused
    // Allocate memory for build commands
    unsigned int size = get_array_length(c_config.src);
    char* file_cmd[size];
    pthread_t build_file_threads[size];
    for (unsigned int i = 0; i < size; i++) {
        file_cmd[i] = malloc(PATH_MAX);
    }

    // Create the build commands for each source file
    if (make_targets(&c_config, file_cmd, size) != 0) {
        fprintf(stderr, "Error: make_build_targets failed\n");
        return -1;
    }

    // Run the build commands for each source file
    int files_built = 0;
    for (unsigned int i = 0; i < size; i++) {
        if (internal_config->thread_count > 0) {
            if (file_cmd[i][0] == '\0') {
                continue;
            }
            while (true) {
                pthread_mutex_lock(&g_thread_count_mutex);
                if (g_thread_count < MAX_CPU_CORES) {
                    pthread_mutex_unlock(&g_thread_count_mutex);
                    break;
                }
                pthread_mutex_unlock(&g_thread_count_mutex);
                usleep(100);
            }
            pthread_mutex_lock(&g_thread_count_mutex);
            g_thread_count = g_thread_count + 1;
            pthread_mutex_unlock(&g_thread_count_mutex);
            if (pthread_create(&build_file_threads[i], NULL, (void *(*)(void *))build_file, file_cmd[i]) != 0) {
                fprintf(stderr, "Error: pthread_create failed\n");
                return -1;
            }
            files_built++;
        } else {
            if (file_cmd[i][0] == '\0') {
                continue;
            }
            build_file(file_cmd[i]);
            files_built++;
        }
    }

    // Wait for all threads to finish if multithreaded
    for (unsigned int i = 0; i < size; i++) {
        if (internal_config->thread_count > 0) {
            if (file_cmd[i][0] == '\0') {
                continue;
            }
            pthread_join(build_file_threads[i], NULL);
        }
        free(file_cmd[i]);
    }


    return files_built;
} // }}}
bool compile_exe(const config_t *config)
{ // {{{
    char build_exe_cmd[PATH_MAX];
    if (make_executable(config, build_exe_cmd) != 0) {
        fprintf(stderr, "Error: make_build_executable failed\n");
        return false;
    }
    if (exec("%s", build_exe_cmd) != 0) {
        fprintf(stderr, "Error: Failed to run the build command\n");
        return false;
    }
    return true;
} // }}}

// Parse command line arguments
bool parse_args(struct InternalConfig *conf, int argc, const char *const argv[])
{ // {{{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--") == 0) {
            conf->run_argc = i;
            conf->run = true;
            break;
        }
        if (!strcmp(argv[i], "dbg")) conf->mode = MODE_DEV;
        else if (!strcmp(argv[i], "rel")) conf->mode = MODE_REL;
        else if (!strcmp(argv[i], "clean")) conf->clean = true;
        else if (!strcmp(argv[i], "build-only")) conf->build_only = true;
        else if (!strcmp(argv[i], "version")) { printf("Build version %s\n", build.ver); return false; }
        else if (!strcmp(argv[i], "help")) { print_help(); return false; }
        else if (argv[i][0] == 'j') {
            int threads = 0;
            if (strlen(argv[i]) > 1) {
                const char *p = argv[i]+1;
                threads = atoi(p);
            } else if (i + 1 <= argc) {
                threads = atoi(argv[++i]);
            } else {
                fprintf(stderr, "Error: Failed to pass thread count\n");
                exit(1);
            }
            print("INF", "1", "Threads: %d\n", threads);
            conf->thread_count = threads;

        }
        else {
            print("ERROR", "31", "Unknown command '%s'\n\n", argv[i]);
            return false;
        }
    }
    return true;
} // }}}

int main(int argc, const char *const argv[])
{ // {{{
    struct InternalConfig conf = {0};
    conf.thread_count = MAX_CPU_CORES;
    if (!parse_args(&conf, argc, argv)) {
        return 0;
    }

    if (conf.clean) {
        // Clean the build directory
        if (exec("rm -rf %s", c_config.dir) != 0) {
            fprintf(stderr, "Error: Failed to clean the build directory %s\n", c_config.dir);
            return -1;
        }
        print("INF", "1", "Cleaned: %s\n", c_config.dir);
        return 0; // Exit after cleaning
    }

    // Create the build directory if it doesn't exist
    recursive_mkdir(c_config.dir);

    // Get the lock file path and deserialize the lock file
    char lock_file_path[PATH_MAX];
    snprintf(lock_file_path, PATH_MAX, "%s/%s.lock", c_config.dir, build.exe);
    if (!deserialize_lock_file(lock_file_path, &lockfile)) {
        serialize_lock_file(lock_file_path, &lockfile);
    }

    // Check if the build is locked
    if (lockfile.lock) {
        time_t current_time = time(NULL);
        fprintf(stderr, "Error: Build is locked. Last modified: %ld seconds ago\n",
                current_time - lockfile.last_build);
        return -1;
    }
    lockfile.lock = true;

    // Build the build file if it has changed
    int ret = compile_build_file(lock_file_path, argc, argv, &conf);
    if (ret != 0) {
        lockfile.last_mode = conf.mode;
        lockfile.lock = false;
        lockfile.rebuilding = false;
        serialize_lock_file(lock_file_path, &lockfile);
        return ret;
    }

    // If only building the build file, exit after building
    if (conf.build_only) {
        print("INF", "1", "Build only mode\n");
        lockfile.last_mode = conf.mode;
        lockfile.lock = false;
        lockfile.rebuilding = false;
        serialize_lock_file(lock_file_path, &lockfile);
        return 0;
    }

    // Compile the source files if they have changed
    int files_built = compile_files(&c_config, &conf);

    // Run the build command to create the executable
    if (files_built != 0) {
        if (!compile_exe(&c_config)) {
            fprintf(stderr, "Error: Failed to compile the executable\n");
            lockfile.last_mode = conf.mode;
            lockfile.lock = false;
            lockfile.rebuilding = false;
            serialize_lock_file(lock_file_path, &lockfile);
            return -1;
        }
    } else {
        print("INF", "1", "No files were changed\n");
    }

    // Update the lock file after the build is complete and release the lock
    lockfile.last_build = time(NULL);
    lockfile.lock = false;
    lockfile.last_mode = conf.mode;
    lockfile.rebuilding = false; // Reset rebuilding state after a successful build

    // Serialize the lock file to save the state
    serialize_lock_file(lock_file_path, &lockfile);

    if (conf.run) {
        char cmd[PATH_MAX];
        snprintf(cmd, PATH_MAX, "%s/%s", c_config.dir, c_config.exe);
        for (int i = conf.run_argc + 1; i < argc; i++)
            snprintf(cmd + strlen(cmd), PATH_MAX - strlen(cmd), " %s", argv[i]);
        return exec(cmd);
    }

    return 0;
} // }}}

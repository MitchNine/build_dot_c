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

const char* cc = "clang";
const char* c_exe = "example_app";
const char* c_src[] =
{
    "src/main.c",
};
char* c_cflags[] = {
	"-MD",
#if defined(DEBUG)
	"-DDEBUG", "-g",
#elif defined(RELEASE)
	"-DRELEASE", "-O3",
#endif
	"-Wall",
};
char* c_clibs[] = {
    "-lpthread"
};

const char* build_cc  = "clang";
const char* build_c   = "./build.c";
const char* build_dir = "./out";
const char* build_exe = "./build";
const char* build_ver = "0.8.3";

/*****************************************************************************
 *****************************************************************************/

#include <stdio.h>
void print_help() {
    printf("\n"
"██████╗ ██╗   ██╗██╗██╗     ██████╗     ██████╗\n"
"██╔══██╗██║   ██║██║██║     ██╔══██╗   ██╔════╝\n"
"██████╔╝██║   ██║██║██║     ██║  ██║   ██║     \n"
"██╔══██╗██║   ██║██║██║     ██║  ██║   ██║     \n"
"██████╔╝╚██████╔╝██║███████╗██████╔╝██╗╚██████╗\n"
"╚═════╝  ╚═════╝ ╚═╝╚══════╝╚═════╝ ╚═╝ ╚═════╝\n\n"
"Usage: ./build [dev|rel|clean|help] -- [ARGS]...\n"
"Builds C/C++ target applications using the configuration provided in the\n"
"build.c file. The build executable will rebuild itself when changes are\n"
"detected within the build.c file.\n\n"
"Command options:\n"
"    dev            Build the target executable with -DDEBUG enabled\n"
"    rel            Build the target executable with -DRELEASE enabled\n"
"    clean          Removes the output directory\n"
"    build-only     Only builds the build executable not the target executable\n"
"    version        Displays the version of the build.c\n"
"    help           Displays this text\n"
"    --             Runs the executable and all args after the double dashes\n"
"                   will be passed onto the executable.\n\n"
"configurations to be set in the build.c file\n"
"    cc             Compiler to use for building target and build.c\n"
"    c_exe          Target executable name\n"
"    c_src          List of all the .c files to be compiled\n"
"    c_cflags       List of all the flags that will be sent to the compiler\n"
"    build_cc       Compiler to use for building the build.c file\n"
"    build_c        Path to the build.c file from the working directory\n"
"    build_dir      The output directory that the target executable will end up\n"
"    build_exe      The name of the executable the build.c file will compile into\n"
" Example:\n"
"    ./build dev -- --file=./output/\n");
}

#include <assert.h>
#include <errno.h>
#include <libgen.h>
#include <linux/limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define STAT_FILE(file_path) \
({ struct stat __stat; stat(file_path, &__stat); __stat; })

enum Mode { MODE_NONE, MODE_DEV, MODE_REL };
struct BuildContext { bool lock; time_t last_build; time_t last_modif; enum Mode last_mode; };
struct Config { bool run; bool clean; bool build_only; int run_argc; enum Mode mode; };
struct Path { char dir[PATH_MAX]; char base[PATH_MAX]; };

struct Path split_path(const char *path) {
    struct Path parts = {{0}, {0}};
    char *tmp1 = strdup(path), *tmp2 = strdup(path);
    snprintf(parts.base, sizeof(parts.base), "%s", basename(tmp1));
    snprintf(parts.dir, sizeof(parts.dir), "%s", dirname(tmp2));
    free(tmp1); free(tmp2);
    return parts;
}
int exec(const char *format, ...) {
    char cmd[PATH_MAX], buff[PATH_MAX];
    va_list val;
    va_start(val, format);
    vsprintf(cmd, format, val);
    va_end(val);
    printf("[\e[35mCMD\e[0m] %s\n", cmd);
    fflush(stdout);
    FILE *fp = popen(cmd, "r");
    assert(fp && "popen failed");
    while (fgets(buff, sizeof(buff), fp) != NULL) printf("%s", buff);
    return pclose(fp);
}
int rec_mkdir(const char *dir) {
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", dir);
    for (char *p = tmp + 1; *p; p++) if (*p == '/') { *p = 0; mkdir(tmp, S_IRWXU); *p = '/'; }
    return mkdir(tmp, S_IRWXU);
}
void append_flags(char *cmd, char *const *flags, int n) {
    for (int i = 0; i < n; i++) snprintf(cmd + strlen(cmd), PATH_MAX - strlen(cmd), "%s ", flags[i]);
}

void get_build(struct BuildContext *ctx, const char *lockfile) {
    FILE *build_file_info;
    if ((build_file_info = fopen(lockfile, "r"))) {
        assert(fread(ctx, sizeof(struct BuildContext), 1, build_file_info) > 0);
        fclose(build_file_info);
    } else if ((build_file_info = fopen(lockfile, "w")))
        fclose(build_file_info);
}
void set_build(const struct BuildContext *ctx, const char *lockfile) {
    FILE *build_file_info;
    if ((build_file_info = fopen(lockfile, "w"))) {
        assert(fwrite(ctx, sizeof(struct BuildContext), 1, build_file_info) > 0);
        fclose(build_file_info);
    }
}

// Helper to copy a filename and strip its extension (in-place)
void strip_extension(const char *src, char *dst, size_t dst_size) {
    snprintf(dst, dst_size, "%s", src);
    char *dot = strrchr(dst, '.');
    if (dot) *dot = '\0';
}

// Helper to generate an object or dependency file path
void build_file_path(char *dst, size_t dst_size, const char *dir, const char *subdir, const char *base, const char *ext) {
    char base_noext[PATH_MAX];
    strip_extension(base, base_noext, sizeof(base_noext));
    snprintf(dst, dst_size, "%s/%s/%s%s", dir, subdir, base_noext, ext);
}

bool check_deps(const char *path, const struct BuildContext *ctx) {
    char output_file[PATH_MAX];
    struct stat source_file_stat = STAT_FILE(path);
    if (source_file_stat.st_mtim.tv_sec >= ctx->last_build) return true;
    struct Path p = split_path(path);
    char *line = NULL;
    bool dep_has_changed = false;
    build_file_path(output_file, sizeof(output_file), build_dir, p.dir, p.base, ".d");
    FILE *depfile = fopen(output_file, "r");
    if (!depfile) return true;
    size_t line_len = 0;
    while (getline(&line, &line_len, depfile) != -1) {
        char *tok = strtok(line, " ");
        bool first = true;
        while (tok != NULL) {
            if (first) { first = false; tok = strtok(NULL, " "); continue; }
            size_t len = strlen(tok);
            if (tok[len - 1] == '\n') tok[--len] = 0;
            struct stat filestat = STAT_FILE(tok);
            if (filestat.st_mtim.tv_sec >= ctx->last_build) dep_has_changed = true;
            tok = strtok(NULL, " ");
        }
    }
    if (line) free(line);
    fclose(depfile);
    return dep_has_changed;
}
int compile_file(const char *path, const struct BuildContext *ctx, bool *changed) {
    char cmd[PATH_MAX], output_dir[PATH_MAX], obj_file[PATH_MAX];
    struct Path p = split_path(path);
    *changed = check_deps(path, ctx);
    if (!*changed) return 0;
    snprintf(output_dir, PATH_MAX, "%s/%s", build_dir, p.dir);
    struct stat st = {0};
    if (stat(output_dir, &st) == -1) if (rec_mkdir(output_dir) == -1) fprintf(stderr, "Failed %s\n", strerror(errno));
    build_file_path(obj_file, sizeof(obj_file), build_dir, p.dir, p.base, ".o");
    snprintf(cmd, PATH_MAX, "%s -c %s -o %s ", cc, path, obj_file);
    append_flags(cmd, c_cflags, ARRAY_SIZE(c_cflags));
    return exec(cmd);
}
bool compile_exe(void) {
    char cmd[PATH_MAX] = {0}, obj_file[PATH_MAX];
    snprintf(cmd, PATH_MAX, "%s -o %s/%s ", cc, build_dir, c_exe);
    append_flags(cmd, c_cflags, ARRAY_SIZE(c_cflags));
    for (int i = 0; i < (sizeof(c_src) / sizeof(c_src[0])); i++) {
        const char *path = c_src[i];
        struct Path p = split_path(path);
        build_file_path(obj_file, sizeof(obj_file), build_dir, p.dir, p.base, ".o");
        snprintf(cmd + strlen(cmd), PATH_MAX - strlen(cmd), "%s ", obj_file);
    }
    append_flags(cmd, c_clibs, ARRAY_SIZE(c_clibs));
    return (exec(cmd) == 0);
}

bool parse_args(struct Config* conf, int argc, char *const argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--") == 0) {
            conf->run_argc = i;
            conf->run = true;
            break;
        }
        if (!strcmp(argv[i], "dev")) conf->mode = MODE_DEV;
        else if (!strcmp(argv[i], "rel")) conf->mode = MODE_REL;
        else if (!strcmp(argv[i], "clean")) conf->clean = true;
        else if (!strcmp(argv[i], "build-only")) conf->build_only = true;
        else if (!strcmp(argv[i], "version")) { printf("Build version %s\n", build_ver); }
        else if (!strcmp(argv[i], "help")) { print_help(); return false; }
        else {
            printf("[\033[31mERROR\033[0m] Unknown command '%s'\n\n", argv[i]);
            print_help();
            return false;
        }
    }
    return true;
}

int main(int argc, char *argv[]) {
    struct Config conf = {0};
    if (!parse_args(&conf, argc, argv)) return 0;

    char build_file_lock[PATH_MAX];
    snprintf(build_file_lock, PATH_MAX, "%s/%s.lock", build_dir, build_exe);

    struct BuildContext build_ctx = {0};
    get_build(&build_ctx, build_file_lock);
    if (build_ctx.last_mode != conf.mode || conf.clean) {
        set_build(&build_ctx, build_file_lock);
        struct stat st = {0};
        if (stat(build_dir, &st) == 0) { exec("rm -dr %s", build_dir); }
    }

    struct stat st = {0};
    if (stat(build_dir, &st) == -1) { if (rec_mkdir(build_dir) == -1) { printf("Failed %s\n", strerror(errno)); } }
    if (build_ctx.lock) { printf("[\033[31mERROR\033[0m] Build in progress\n"); return 1; }
    else { build_ctx.lock = true; set_build(&build_ctx, build_file_lock); }

    struct stat build_file = STAT_FILE(build_c);
    if ((build_ctx.last_modif < build_file.st_mtim.tv_sec) || build_ctx.last_mode != conf.mode || conf.build_only) {
        int ret;
        if (conf.mode == MODE_DEV) {
            build_ctx.last_mode = MODE_DEV;
            if ((ret = exec("%s %s -o %s -DDEBUG -g -Wall", build_cc, build_c, build_exe))) return ret;
        } else if (conf.mode == MODE_REL) {
            build_ctx.last_mode = MODE_REL;
            if ((ret = exec("%s %s -o %s -DRELEASE -O3 -Wall", build_cc, build_c, build_exe))) return ret;
        } else {
            build_ctx.last_mode = MODE_NONE;
            if ((ret = exec("%s %s -o %s -Wall", build_cc, build_c, build_exe))) return ret;
        }
        build_ctx.last_modif = time(NULL);
        build_ctx.last_build = time(NULL);
        build_ctx.lock = false;
        set_build(&build_ctx, build_file_lock);
        if (!conf.build_only) {
            char cmd[PATH_MAX];
            snprintf(cmd, PATH_MAX, "%s", build_exe);
            for (int i = 1; i < argc; i++) {
                if (strcmp(argv[i], "clean") == 0) continue;
                snprintf(cmd + strlen(cmd), PATH_MAX - strlen(cmd), " %s", argv[i]);
            }
            return exec(cmd);
        } else return 0;
    }
    bool change = false, file_changed = false;
    for (int i = 0; i < (sizeof(c_src) / sizeof(c_src[0])); i++) {
        int ret = compile_file(c_src[i], &build_ctx, &file_changed);
        if (file_changed && ret != 0) {
            printf("[\033[31mERROR\033[0m] Failed to build file\n");
            build_ctx.lock = false;
            set_build(&build_ctx, build_file_lock);
            return 2;
        }
        change = change || file_changed;
    }
    if (change && !compile_exe()) {
        printf("[\033[31mERROR\033[0m] Failed to build exe\n");
        build_ctx.lock = false;
        set_build(&build_ctx, build_file_lock);
        return 2;
    }
    build_ctx.last_build = time(NULL);
    build_ctx.lock = false;
    set_build(&build_ctx, build_file_lock);
    int ret = 0;
    if (conf.run) {
        char cmd[PATH_MAX];
        snprintf(cmd, PATH_MAX, "%s/%s", build_dir, c_exe);
        for (int i = conf.run_argc + 1; i < argc; i++)
            snprintf(cmd + strlen(cmd), PATH_MAX - strlen(cmd), " %s", argv[i]);
        ret = exec(cmd);
    }
    return ret;
}

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#define PATH_MAX 4096

// --- Utility function prototypes from build.c ---
void strip_extension(const char *src, char *dst, size_t dst_size);
void get_filename_without_path(const char *src, char *dst, size_t dst_size);
void get_path_without_filename(const char *src, char *dst, size_t dst_size);
unsigned int get_array_length(const char *const *array);

// --- Utility function implementations (copied from build.c for testing) ---
void strip_extension(const char *src, char *dst, size_t dst_size) {
    snprintf(dst, dst_size, "%s", src);
    char *dot = strrchr(dst, '.');
    if (dot) *dot = '\0';
}
void get_filename_without_path(const char *src, char *dst, size_t dst_size) {
    const char *slash = strrchr(src, '/');
    if (slash) {
        snprintf(dst, dst_size, "%s", slash + 1);
    } else {
        snprintf(dst, dst_size, "%s", src);
    }
    dst[dst_size - 1] = '\0';
}
void get_path_without_filename(const char *src, char *dst, size_t dst_size) {
    snprintf(dst, dst_size, "%s", src);
    char *slash = strrchr(dst, '/');
    if (slash) {
        *slash = '\0';
    } else {
        dst[0] = '\0';
    }
}
unsigned int get_array_length(const char *const *array) {
    unsigned int length = 0;
    while (array[length] != NULL) {
        length++;
    }
    return length;
}

// --- Test cases ---
void test_strip_extension() {
    char dst[PATH_MAX];
    printf("\033[1m%-40s\033[0m\n", "Running test_strip_extension");
    strip_extension("foo/bar.c", dst, sizeof(dst));
    printf("strip_extension('foo/bar.c') -> '%s' (expected 'foo/bar')\n", dst);
    assert(strcmp(dst, "foo/bar") == 0);
    strip_extension("baz.txt", dst, sizeof(dst));
    printf("strip_extension('baz.txt') -> '%s' (expected 'baz')\n", dst);
    assert(strcmp(dst, "baz") == 0);
    strip_extension("noext", dst, sizeof(dst));
    printf("strip_extension('noext') -> '%s' (expected 'noext')\n", dst);
    assert(strcmp(dst, "noext") == 0);
    printf("%-40s [\033[32mPASSED\033[0m]\n", "test_strip_extension");
}
void test_get_filename_without_path() {
    char dst[PATH_MAX];
    printf("\033[1m%-40s\033[0m\n", "Running test_get_filename_without_path");
    get_filename_without_path("foo/bar.c", dst, sizeof(dst));
    printf("get_filename_without_path('foo/bar.c') -> '%s' (expected 'bar.c')\n", dst);
    assert(strcmp(dst, "bar.c") == 0);
    get_filename_without_path("baz.txt", dst, sizeof(dst));
    printf("get_filename_without_path('baz.txt') -> '%s' (expected 'baz.txt')\n", dst);
    assert(strcmp(dst, "baz.txt") == 0);
    get_filename_without_path("noext", dst, sizeof(dst));
    printf("get_filename_without_path('noext') -> '%s' (expected 'noext')\n", dst);
    assert(strcmp(dst, "noext") == 0);
    printf("%-40s [\033[32mPASSED\033[0m]\n", "test_get_filename_without_path");
}
void test_get_path_without_filename() {
    char dst[PATH_MAX];
    printf("\033[1m%-40s\033[0m\n", "Running test_get_path_without_filename");
    get_path_without_filename("foo/bar.c", dst, sizeof(dst));
    printf("get_path_without_filename('foo/bar.c') -> '%s' (expected 'foo')\n", dst);
    assert(strcmp(dst, "foo") == 0);
    get_path_without_filename("baz.txt", dst, sizeof(dst));
    printf("get_path_without_filename('baz.txt') -> '%s' (expected '')\n", dst);
    assert(strcmp(dst, "") == 0);
    get_path_without_filename("/usr/local/bin/test", dst, sizeof(dst));
    printf("get_path_without_filename('/usr/local/bin/test') -> '%s' (expected '/usr/local/bin')\n", dst);
    assert(strcmp(dst, "/usr/local/bin") == 0);
    printf("%-40s [\033[32mPASSED\033[0m]\n", "test_get_path_without_filename");
}
void test_get_array_length() {
    const char *arr1[] = {"a", "b", "c", NULL};
    printf("\033[1m%-40s\033[0m\n", "Running test_get_array_length");
    unsigned int len1 = get_array_length(arr1);
    printf("get_array_length(arr1) -> %u (expected 3)\n", len1);
    assert(len1 == 3);
    const char *arr2[] = {NULL};
    unsigned int len2 = get_array_length(arr2);
    printf("get_array_length(arr2) -> %u (expected 0)\n", len2);
    assert(len2 == 0);
    printf("%-40s [\033[32mPASSED\033[0m]\n", "test_get_array_length");
}


// Helper to run a system command and print the command and exit status
int run_and_log(const char *cmd) {
    int status = system(cmd);
    printf("system('%s') exited with status %d\n", cmd, status);
    return status;
}


// --- Test running build executable as a subprocess ---
void test_run_build_help() {
    if (access("../build", F_OK) != 0) {
        int status = run_and_log("gcc -o ../build ../build.c");
        assert(WIFEXITED(status));
    }
    printf("\033[1m%-40s\033[0m\n", "Running test_run_build_help");
    int status = run_and_log("../build help > build_help.txt");
    assert(WIFEXITED(status));
    FILE *fp = fopen("build_help.txt", "r");
    if (!fp) {
        perror("fopen(build_help.txt)");
        assert(fp);
    }
    char buf[256];
    int found = 0;
    while (fgets(buf, sizeof(buf), fp)) {
        if (strstr(buf, "Usage: ./build")) found = 1;
    }
    fclose(fp);
    printf("test_run_build_help: found=%d\n", found);
    assert(found);
    printf("%-40s [\033[32mPASSED\033[0m]\n", "test_run_build_help");
}

void test_run_build_on_main() {
    // Remove previous output if exists
    printf("\033[1m%-40s\033[0m\n", "Running test_run_build_on_main");
    int rm_status = run_and_log("rm -rf ../out/");
    if (access("../build", F_OK) != 0) {
        int status = run_and_log("gcc -o ../build ../build.c");
        assert(WIFEXITED(status));
    }
    // Run build
    int status = run_and_log("cd .. && ./build");
    assert(WIFEXITED(status));
    status = run_and_log("cd ./build_testcases");
    // Check if output executable exists
    FILE *fp = fopen("../out/example_app", "r");
    if (!fp) {
        perror("fopen(../out/example_app)");
    }
    assert(fp);
    fclose(fp);
    printf("%-40s [\033[32mPASSED\033[0m]\n", "test_run_build_on_main");
}

int main() {
    test_strip_extension();
    test_get_filename_without_path();
    test_get_path_without_filename();
    test_get_array_length();
    test_run_build_help();
    test_run_build_on_main();
    printf("%-40s [\033[32mALL PASSED\033[0m]\n", "All tests");
    return 0;
}

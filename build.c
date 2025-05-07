#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <libgen.h>
#include <errno.h>
#include <assert.h>
#include <time.h>
#include <utime.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>


/*****************************************************************************
 * Settings
 *****************************************************************************/
char* cc      = "clang";
char* c_exe   = "trace";
char* c_src[] =
{
	"src/main.c",
};
char* c_cflags[] =
{
	"-MD", // Needed for header dependance check
#if defined(DEBUG)
	"-g",
	"-pg",
	"-fsanitize=address",
	"-DDEBUG",
#elif defined(RELEASE)
	"-DRELEASE",
	"-O3",
#endif
	"-Wall",
};

// Build files
char* build_c   = "./src/build.c";
char* build_dir = "./out";
char* build_exe = "./build";

// Command executor
char* pre_build[] = { "echo 'start build'" };
char* post_build[] = { "echo 'end build'" };
char* pre_run[] = { "echo 'start run'" };
char* post_run[] = { "echo 'end run'" };
/*****************************************************************************
 *****************************************************************************/

#define BUFFER_SIZE 1023

enum Mode
{
	MODE_NONE,
	MODE_DEV,
	MODE_REL
};
struct BuildContext
{
	bool lock;
	long int last_build;
	long int last_modif;
	enum Mode last_mode;
};
struct Config
{
	bool run;
	bool clean;
	bool build_only;
	int run_argc;
	enum Mode mode;
};

int exec(const char* command) {
	printf("[\033[32mEXEC\033[0m] %s\n", command);
	char buff[BUFFER_SIZE];
	FILE* fp = popen(command, "r");
	assert(fp && "popen failed");
	while (fgets(buff, sizeof(buff), fp) != NULL)
		printf("%s", buff);
	return pclose(fp);
}
int execf(const char* format, ...) {
	char buff[BUFFER_SIZE];
	va_list val;
	va_start(val, format);
	vsprintf(buff, format, val);
	va_end(val);
	return exec(buff);
}
int rec_mkdir(const char *dir) {
	char tmp[BUFFER_SIZE];
	snprintf(tmp, sizeof(tmp),"%s",dir);
	size_t len = strlen(tmp);
	tmp[len - 1] = tmp[len - 1] == '/' ? 0 : tmp[len - 1];
	for (char* p = tmp + 1; *p; p++)
		if (*p == '/') {
			*p = 0;
			mkdir(tmp, S_IRWXU);
			*p = '/';
		}
	return mkdir(tmp, S_IRWXU);
}

void get_build(struct BuildContext* ctx, char* lockfile) {
	FILE* build_file_info;
	if ((build_file_info = fopen(lockfile, "r"))) {
		long int mod = 0;
		assert(fread(&ctx->last_build, sizeof(long int), 1, build_file_info) > 0);
		assert(fread(&ctx->last_modif, sizeof(long int), 1, build_file_info) > 0);
		assert(fread(&ctx->last_mode, sizeof(enum Mode), 1, build_file_info) > 0);
		assert(fread(&ctx->lock, sizeof(bool), 1, build_file_info) > 0);
		fclose(build_file_info);
	} else if ((build_file_info = fopen(lockfile, "w"))) fclose(build_file_info);
}
void set_build(struct BuildContext* ctx, char* lockfile) {
	FILE* build_file_info;
	if ((build_file_info = fopen(lockfile, "w"))) {
		long int mod = 0;
		assert(fwrite(&ctx->last_build, sizeof(long int), 1, build_file_info) > 0);
		assert(fwrite(&ctx->last_modif, sizeof(long int), 1, build_file_info) > 0);
		assert(fwrite(&ctx->last_mode, sizeof(enum Mode), 1, build_file_info) > 0);
		assert(fwrite(&ctx->lock, sizeof(bool), 1, build_file_info) > 0);
		fclose(build_file_info);
	}
}

bool check_deps(const char* path, struct BuildContext* ctx) {
	char output_file[BUFFER_SIZE];
	struct stat source_filestat;
	assert(stat(path, &source_filestat) >= 0 && "Failed to stat source file");
	if (source_filestat.st_mtim.tv_sec >= ctx->last_build) return true;
	char *out_dir  = dirname(strdup(path)),
		   *out_file = strtok(basename(strdup(path)), "."),
	     *line = NULL;
	bool dep_has_changed = false;
	sprintf(output_file, "%s/%s/%s.d", build_dir, out_dir, out_file);
	FILE* depfile = fopen(output_file, "r");
	if (!depfile) { return true; }
	while (getline(&line, NULL, depfile) != -1) {
		char* linecpy = strdup(line);
		char* tok = strtok(linecpy, " ");
		while (tok != NULL) {
			int strlen = strnlen(tok, BUFFER_SIZE);
			if (tok[strlen - 1] == '\n') tok[(strlen--) - 1] = 0;
			if ((tok[strlen] != '\0' || tok[strlen - 1] != 'h')) {
				tok = strtok(NULL, " ");
				continue;
			}
			if (!strstr(tok, "src")) {
				tok = strtok(NULL, " ");
				continue;
			}
			struct stat filestat;
			assert(stat(tok, &filestat) >= 0 && "Failed to stat file");
			printf("%ld >= %ld\n", filestat.st_mtim.tv_sec, ctx->last_build);
			if (filestat.st_mtim.tv_sec >= ctx->last_build) dep_has_changed = true;
			tok = strtok(NULL, " ");
		}
		free(linecpy);
	}
	if (line) free(line);
	fclose(depfile);
	return dep_has_changed;
}
int compile_file(const char* path, struct BuildContext* ctx, bool* changed) {
	char cmd[BUFFER_SIZE], output_dir[BUFFER_SIZE], output_file[BUFFER_SIZE];
	char* out_dir = dirname(strdup(path)), *out_file = strtok(basename(strdup(path)), ".");
	*changed = check_deps(path, ctx);
	if (!*changed) return 0;
	sprintf(output_dir, "%s/%s", build_dir, out_dir);
	struct stat st = {0};
	if (stat(output_dir, &st) == -1)
		if (rec_mkdir(output_dir) == -1)
			printf("Failed %s\n", strerror(errno));
	sprintf(cmd, "%s -c %s -o %s/%s/%s.o ", cc, path, build_dir, out_dir, out_file);
	for (int i = 0; i < (sizeof(c_cflags) / sizeof(c_cflags[0])); i++)
		sprintf(cmd + strlen(cmd), "%s ", c_cflags[i]);
	return exec(cmd);
}
bool compile_exe() {
	char cmd[BUFFER_SIZE] = {0};
	sprintf(cmd, "%s -o %s/%s ", cc, build_dir, c_exe);
	for (int i = 0; i < (sizeof(c_cflags) / sizeof(c_cflags[0])); i++)
		sprintf(cmd + strlen(cmd), "%s ", c_cflags[i]);
	for (int i = 0; i < (sizeof(c_src) / sizeof(c_src[0])); i++) {
		char* path = c_src[i], * out_dir = dirname(strdup(path)), *out_file = strtok(basename(strdup(path)), ".");
		sprintf(cmd + strlen(cmd), "%s/%s/%s.o ", build_dir, out_dir, out_file);
	}
	return (exec(cmd) == 0);
}
void print_help() {
	printf(
"\n██████╗ ██╗   ██╗██╗██╗     ██████╗     ██████╗\n"
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
"    help           Displays this text\n"
"    --             Runs the executable and all args after the double dashes\n"
"                   will be passed onto the executable.\n\n"
"configurations to be set in the build.c file\n"
"    cc             Compiler to use for building target and build.c\n"
"    c_exe          Target executable name\n"
"    c_src          List of all the .c files to be compiled\n"
"    c_cflags       List of all the flags that will be sent to the compiler\n"
"    build_c        Path to the build.c file from the working directory\n"
"    build_dir      The output directory that the target executable will end up\n"
"    build_exe      The name of the executable the build.c file will compile into\n"
"    pre_build      List of commands to run before compiling the source files\n"
"    post_build     List of commands to run after compiling the source files\n"
"    pre_run        List of commands to run before running the target executable\n"
"    post_run       List of commands to run after running the target executable\n\n"
" Example:\n"
"    ./build dev -- --file=./output/\n"
			);
}
int main(int argc, char *argv[]) {
	struct Config conf = {};
	conf.mode = MODE_NONE;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--") == 0) {
			conf.run_argc = i;
			conf.run = true;
			break;
		}
		else if (strcmp(argv[i], "dev") == 0) { conf.mode = MODE_DEV; }
		else if (strcmp(argv[i], "rel") == 0) { conf.mode = MODE_REL; }
		else if (strcmp(argv[i], "clean") == 0) { conf.clean = true; }
		else if (strcmp(argv[i], "build-only") == 0) { conf.build_only = true; }
		else if (strcmp(argv[i], "help") == 0) { print_help(); return 0; }
		else {
			printf("[\033[31mERROR\033[0m] Unknown command '%s'\n\n", argv[i]);
			print_help();
			return 1;
		}
	}


	char build_file_lock[BUFFER_SIZE];
	sprintf(build_file_lock, "%s/%s.lock", build_dir, build_exe);

	struct BuildContext build_ctx = {0};
	get_build(&build_ctx, build_file_lock);
	if (build_ctx.last_mode != conf.mode || conf.clean) {
			set_build(&build_ctx, build_file_lock);
			struct stat st = {0};
			if (stat(build_dir, &st) == 0) {
				execf("rm -dr %s", build_dir);
			}
	}

	struct stat st = {0};
	if (stat(build_dir, &st) == -1) {
		if (rec_mkdir(build_dir) == -1) {
			printf("Failed %s\n", strerror(errno));
		}
	}

	if (build_ctx.lock) {
		printf("[\033[31mERROR\033[0m] Build in progress\n");
		return 1;
	} else {
		build_ctx.lock = true;
		set_build(&build_ctx, build_file_lock);
	}

	struct stat build_filestat;
	assert(stat(build_c, &build_filestat) >= 0 && "Failed to stat build.c");
	if ((build_ctx.last_modif < build_filestat.st_mtim.tv_sec) ||
			build_ctx.last_mode != conf.mode || conf.build_only) {
		int ret;
		switch (conf.mode) {
			case MODE_DEV: build_ctx.last_mode = MODE_DEV;
				if ((ret = execf("%s %s -o %s -DDEBUG", cc, build_c, build_exe))) return ret;
				break;
			case MODE_REL: build_ctx.last_mode = MODE_REL;
				if ((ret = execf("%s %s -o %s -DRELEASE", cc, build_c, build_exe))) return ret;
				break;
			default: build_ctx.last_mode = MODE_NONE;
				if ((ret = execf("%s %s -o %s", cc, build_c, build_exe))) return ret;
		}
		build_ctx.last_modif = time(NULL);
		build_ctx.last_build = time(NULL);
		build_ctx.lock = false;
		set_build(&build_ctx, build_file_lock);
		if (!conf.build_only) {
			char cmd[BUFFER_SIZE];
			sprintf(cmd, "%s", build_exe);
			for (int i = 1; i < argc; i++) {
				if (strcmp(argv[i], "clean") == 0) continue;
				sprintf(cmd, "%s %s", cmd, argv[i]);
			}
			ret = exec(cmd);
			return ret;
		} else {
			return 0;
		}
	}

	for (int i = 0; i < sizeof(pre_build) / sizeof(pre_build[0]); i++)
		exec(pre_build[i]);

	bool change = false;
	for (int i = 0; i < (sizeof(c_src) / sizeof(c_src[0])); i++) {
		int ret = compile_file(c_src[i], &build_ctx, &change);
		if (change && ret != 0) {
			printf("[\033[31mERROR\033[0m] Failed to build file\n");
			build_ctx.lock = false;
			set_build(&build_ctx, build_file_lock);
			return 2;
		}
	}
	if (change) {
		if (!compile_exe()) printf("[\033[31mERROR\033[0m] Failed to build exe\n");
	} else { printf("[\033[36mINF\033[0m] Nothing to recompile\n"); }
	build_ctx.last_build = time(NULL);
	build_ctx.lock = false;
	set_build(&build_ctx, build_file_lock);

	for (int i = 0; i < sizeof(post_build) / sizeof(post_build[0]); i++)
		exec(post_build[i]);
	for (int i = 0; i < sizeof(pre_run) / sizeof(pre_run[0]); i++)
		exec(pre_run[i]);

	int ret = 0;
	if (conf.run) {
		char cmd[BUFFER_SIZE];
		sprintf(cmd, "%s/%s", build_dir, c_exe);
		for (int i = conf.run_argc + 1; i < argc; i++)
			sprintf(cmd, "%s %s", cmd, argv[i]);
		ret = exec(cmd);
	}

	for (int i = 0; i < sizeof(post_run) / sizeof(post_run[0]); i++)
		exec(post_run[i]);

	return ret;
}

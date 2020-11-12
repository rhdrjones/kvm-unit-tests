#include <libcflat.h>
#include <chr-testdev.h>

static void usage(void)
{
	puts(
		"usage: cmd <subcmd> [args...]\n"
		"  subcmds:\n"
		"  env			dump environment\n"
		"  quit			quit with chr-testdev\n"
		"\n"
	);
}

int main(int argc, char **argv, char **envp)
{
	int i;

	printf("\nCommand: %s", argv[0]);
	for (i = 1; i < argc; ++i)
		printf(" %s", argv[i]);
	puts("\n\n");

	if (argc < 2)
		goto usage;

	if (strcmp(argv[1], "env") == 0) {
		while (*envp) {
			printf("%s\n", *envp);
			++envp;
		}
		puts("\n");
	} else if (strcmp(argv[1], "quit") == 0) {
		chr_testdev_exit(0);
	} else {
		goto usage;
	}

	return 0;

usage:
	usage();
	return 1;
}

#include <stdio.h>
#include <stdlib.h>
#include <linux/limits.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/wait.h>

#define TEMPLATE "/brn.XXXXXX"

struct fname {
	char name[NAME_MAX];
};

struct flist {
	struct fname *files;
	size_t len;
};

void
cmd(char **argv)
{
	pid_t child = fork();

	if (child == 0) {
		if (execvp(argv[0], argv) < 0) {
			fprintf(stderr,
				"[ERROR]: could not execute the child: %s\n",
				strerror(errno));
			exit(1);
		}
	} else if (child > 0) {
		int wstatus;
		if (wait(&wstatus) < 0) {
			fprintf(stderr,
				"[ERROR]: could not wait for the forked child: %s\n",
				strerror(errno));
			exit(1);
		}
	} else {
		fprintf(stderr, "[ERROR]: could not fork a child: %s\n",
			strerror(errno));
		exit(1);
	}
}

struct flist
files_in_dir(char *dirname)
{
	DIR *dir = opendir(dirname);
	if (dir == NULL) {
		fprintf(stderr, "[ERROR] could not open directory %s\n",
			dirname);
		exit(1);
	}

	struct flist r;

	size_t actual_length = 100;
	r.files = malloc(sizeof(struct fname) * actual_length);

	struct dirent *iter;
	size_t counter = 0;
	while ((iter = readdir(dir)) != NULL) {
		if (strcmp(iter->d_name, ".") == 0)
			continue;
		if (strcmp(iter->d_name, "..") == 0)
			continue;

		if (counter == actual_length) {
			actual_length *= 2;
			r.files = realloc(r.files,
					  sizeof(struct fname) * actual_length);
		}

		strcpy(r.files[counter].name, iter->d_name);
		counter++;
	}

	r.len = counter;

	closedir(dir);

	return r;
}

struct flist
split_lines(char *filename)
{
	FILE *fptr = fopen(filename, "r");
	if (!fptr) {
		fprintf(stderr, "[ERROR] could not open file %s\n", filename);
	}

	struct flist r;

	size_t actual_length = 100;
	r.files = malloc(sizeof(struct fname) * actual_length);

	size_t counter = 0;
	while (true) {
		if (feof(fptr)) {
			break;
		}
		fgets(r.files[counter].name, NAME_MAX, fptr);
		size_t len = strlen(r.files[counter].name);
		r.files[counter].name[len - 1] = '\0';
		counter++;
	}
	fclose(fptr);

	r.len = counter - 1;

	return r;
}

void
execute(struct flist old, struct flist new)
{
	bool abort = false;

	if (old.len != new.len) {
		fprintf(stderr,
			"[ABORTING] You are renaming %zu files but buffer contains %zu file names\n",
			old.len, new.len);
		abort = true;
	}

	for (size_t i = 0; i < new.len; ++i) {
		for (size_t j = i + 1; j < new.len; ++j) {
			if (strcmp(new.files[i].name, new.files[j].name) == 0) {
				fprintf(stderr,
					"[ABORTING] \"%s\" appears more than once in the buffer\n",
					new.files[i].name);
				abort = true;
			}
		}
	}

	if (abort)
		exit(1);

	bool tempmode = false;
	for (size_t i = 0; i < new.len; ++i) {
		for (size_t j = 0; j < old.len; ++j) {
			if (strcmp(new.files[i].name, old.files[j].name) == 0) {
				printf("Cycle detected\n");
				tempmode = true;
			}
		}
	}

	if (tempmode) {
	} else {
		for (size_t i = 0; i < old.len; ++i) {
			rename(old.files[i].name, new.files[i].name);
		}
	}
}

int
main(int argc, char *argv[])
{
	/* Usage:
	 * input can be single directory
	 * input can be individual file names
	 */

	char *editor_cmd = getenv("EDITOR");
	if (!editor_cmd)
		editor_cmd = getenv("VISUAL");
	if (!editor_cmd) {
		fprintf(stderr,
			"[ERROR] $EDITOR and $VISUAL are both not set in the environment\n");
	}

	char *tempdir = getenv("TMPDIR");
	if (!tempdir) {
		tempdir = "/tmp";
	}

	char tempfile[PATH_MAX];
	strncpy(tempfile, tempdir, PATH_MAX);
	strcat(tempfile, TEMPLATE);

	int fd = mkstemp(tempfile);
	close(fd);

	struct flist old = files_in_dir(".");

	FILE *fptr = fopen(tempfile, "r+");
	if (!fptr) {
		fprintf(stderr, "[ERROR] could not open file %s\n", tempfile);
	}

	for (size_t i = 0; i < old.len; ++i) {
		fprintf(fptr, "%s\n", old.files[i].name);
	}
	fclose(fptr);

	char *args[] = { editor_cmd, tempfile, NULL };
	cmd(args);

	struct flist new = split_lines(tempfile);

	execute(old, new);

	unlink(tempfile);

	if (old.files)
		free(old.files);

	if (new.files)
		free(new.files);

	return 0;
}

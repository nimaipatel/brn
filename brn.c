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
#include <fcntl.h>
#include <sys/syscall.h>

#define TEMPLATE "/brn.XXXXXX"

/* to store a file name */
struct fname {
	char name[NAME_MAX];
};

/* to store list of file names */
struct flist {
	struct fname *files;
	size_t len;
};

/* execute external command */
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

/* generate flist from contents of directory `dirname` */
struct flist
flist_from_dir(char *dirname)
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

/* generate flist from new-line separated file names in `filename` */
struct flist
flist_from_lines(char *filename)
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
verify(struct flist old, struct flist new)
{
	bool abort = false;

	/* number of files is greater or lesser than number we are renaming */
	if (old.len != new.len) {
		fprintf(stderr,
			"[ABORTING] You are renaming %zu files but"
			" buffer contains %zu file names\n",
			old.len, new.len);
		abort = true;
	}

	/* same file name appears more than once */
	for (size_t i = 0; i < new.len; ++i) {
		for (size_t j = i + 1; j < new.len; ++j) {
			if (strcmp(new.files[i].name, new.files[j].name) == 0) {
				fprintf(stderr,
					"[ABORTING] \"%s\" appears more than"
					" once in the buffer\n",
					new.files[i].name);
				abort = true;
			}
		}
	}

	if (abort)
		exit(1);
}

void
print_rules(struct flist f, struct flist g)
{
	for (size_t i = 0; i < f.len; ++i) {
		printf("%s", f.files[i].name);
		printf(" -> ");
		printf("%s", g.files[i].name);
		printf("\n");
	}
	printf("\n");
}

void
execute(struct flist *old, struct flist *new)
{
	size_t len = old->len;

	for (size_t i = 0; i < len; ++i) {
		print_rules(*old, *new);
		char *oldname = old->files[i].name;
		char *newname = new->files[i].name;

		/* Glibc does not  provide  a  wrapper  for  the  renameat2()
		 * system  call*/
		int r = syscall(SYS_renameat2, AT_FDCWD, oldname, AT_FDCWD,
				newname, (1 << 1));
		if (r < 0)
			rename(oldname, newname);

		for (size_t j = i + 1; j < old->len; ++j) {
			if (strcmp(old->files[j].name, newname) == 0) {
				strcpy(old->files[j].name, oldname);
			}
		}
	}
}

int
main()
{
	/* Usage: call in directory which contains files which you want to
	 * rename */

	char *editor_cmd = getenv("EDITOR");
	if (!editor_cmd)
		editor_cmd = getenv("VISUAL");
	if (!editor_cmd) {
		fprintf(stderr, "[ERROR] $EDITOR and $VISUAL are"
				" both not set in the environment\n");
		exit(1);
	}

	char *tempdir = getenv("TMPDIR");
	if (!tempdir) {
		tempdir = "/tmp";
	}

	char tempfile[PATH_MAX];
	/* not using on user provided strings so should be fine */
	strcpy(tempfile, tempdir);
	strcat(tempfile, TEMPLATE);

	int fd = mkstemp(tempfile);
	close(fd);

	struct flist old = flist_from_dir(".");

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

	struct flist new = flist_from_lines(tempfile);

	verify(old, new);

	execute(&old, &new);

	unlink(tempfile);

	if (old.files)
		free(old.files);

	if (new.files)
		free(new.files);

	return 0;
}

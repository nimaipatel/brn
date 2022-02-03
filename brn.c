/*
 * Copyright (C) 2022 Patel, Nimai <nimai.m.patel@gmail.com>
 * Author: Patel, Nimai <nimai.m.patel@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define _USE_GNU
#define _GNU_SOURCE
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

struct flist
flist_from_dir(char *dirname)
{
	struct dirent **namelist;
	int n = scandir(dirname, &namelist, NULL, versionsort);
	if (n < 0) {
		fprintf(stderr, "[ERROR] could not scan directory %s\n",
			dirname);
		exit(1);
	}

	struct flist r;

	r.files = malloc(sizeof(struct fname) * (n - 2));

	size_t counter = 0;
	for (int i = 0; i < n; ++i) {
		if (strcmp(namelist[i]->d_name, ".") == 0) continue;
		if (strcmp(namelist[i]->d_name, "..") == 0) continue;

		strcpy(r.files[counter].name, namelist[i]->d_name);
		counter++;

		free(namelist[i]);
	}
	free(namelist);
	r.len = n - 2;

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

	size_t n_lines = 0;
	char temp_line[NAME_MAX];
	while (fgets(temp_line, sizeof(temp_line), fptr)) {
		n_lines++;
	}
	fseek(fptr, 0, SEEK_SET);

	r.files = malloc(sizeof(struct fname) * n_lines);

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

bool
verify(struct flist old, struct flist new)
{
	bool safe = true;

	/* number of files is greater or lesser than number we are renaming */
	if (old.len != new.len) {
		fprintf(stderr,
			"[ABORTING] You are renaming %zu files but"
			" buffer contains %zu file names\n",
			old.len, new.len);
		safe = false;
	}

	/* same file name appears more than once */
	for (size_t i = 0; i < new.len; ++i) {
		for (size_t j = i + 1; j < new.len; ++j) {
			if (strcmp(new.files[i].name, new.files[j].name) == 0) {
				fprintf(stderr,
					"[ABORTING] \"%s\" appears more than"
					" once in the buffer\n",
					new.files[i].name);
				safe = false;
			}
		}
	}

	return safe;
}

size_t
get_num_renames(struct flist old, struct flist new)
{
	size_t num = 0;
	for (size_t i = 0; i < old.len; ++i) {
		if (strcmp(old.files[i].name, new.files[i].name) != 0) ++num;
	}
	return num;
}

void
execute(struct flist *old, struct flist *new)
{
	size_t len = old->len;

	for (size_t i = 0; i < len; ++i) {
		char *oldname = old->files[i].name;
		char *newname = new->files[i].name;

		if (strcmp(oldname, newname) == 0) continue;

		int r = renameat2(AT_FDCWD, oldname, AT_FDCWD, newname,
				  RENAME_EXCHANGE);
		if (r < 0) rename(oldname, newname);

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
	if (!editor_cmd) editor_cmd = getenv("VISUAL");
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
	strcat(tempfile, "/brn.XXXXXX");

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

	if (verify(old, new)) {
		size_t n_renames = get_num_renames(old, new);
		printf("[SUCCESS] %zu files renamed\n", n_renames);
		execute(&old, &new);

		unlink(tempfile);
		if (old.files) free(old.files);
		if (new.files) free(new.files);

		return 0;
	} else {
		unlink(tempfile);
		if (old.files) free(old.files);
		if (new.files) free(new.files);

		exit(1);
	}
}

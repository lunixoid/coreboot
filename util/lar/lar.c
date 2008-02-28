/*
 * lar - lightweight archiver
 *
 * Copyright (C) 2006-2008 coresystems GmbH
 * (Written by Stefan Reinauer <stepan@coresystems.de> for coresystems GmbH)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA, 02110-1301 USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <getopt.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include "lar.h"
#include "lib.h"

#define VERSION "v0.9.1"
#define COPYRIGHT "Copyright (C) 2006-2008 coresystems GmbH"

static int isverbose = 0;
static int iselfparse = 0;
static long larsize = 0;
static char *bootblock = NULL;
enum compalgo algo = ALGO_NONE;

static void usage(char *name)
{
	printf("\nLAR - the Lightweight Archiver " VERSION "\n" COPYRIGHT "\n\n"
	       "Usage: %s [-ecxal] archive.lar [[[file1] file2] ...]\n\n", name);
	printf("Examples:\n");
	printf("  lar -c -s 32k -b bootblock myrom.lar foo nocompress:bar\n");
	printf("  lar -a myrom.lar foo blob:baz\n");
	printf("  lar -l myrom.lar\n\n");

	printf("File names:\n");
	printf("   Names specified in the create or add modes are formatted as\n");
	printf("   follows:  [flags]:[filename]:[pathname].\n");
	printf("     * Flags are modifiers for the file.  Valid flags:\n");
	printf("       nocompress\tDon't compress the file in the LAR\n");
	printf("     * Filename is the name of the file on disk.  If no pathname\n");
	printf("     is specified, then the filename will also be the path name\n");
	printf("     used in the LAR.\n");
	printf("     * Pathname is the name to use in the LAR header.\n\n");

	printf("Create options:\n");
	printf("  -s [size]     \tSpecify the size of the archive.\n");
	printf("                \tUse a 'k' suffix to multiply the size by 1024 or\n");
	printf("                \ta 'm' suffix to multiple the size by 1024*1024.\n");
	printf("  -b [bootblock]\tSpecify the bootblock blob\n");
	printf("  -C [lzma|nrv2b]\tSpecify the compression method to use\n\n");
	printf("  -e pre-parse the payload ELF into LAR segments. Recommended\n\n");

	printf("General options\n");
	printf("  -v\tEnable verbose mode\n");
	printf("  -V\tShow the version\n");
	printf("  -h\tShow this help\n");
	printf("\n");
}

int elfparse(void)
{
	return iselfparse;
}

/* Add a human touch to the LAR size by allowing suffixes:
 *  XX[KkMm] where k = XX * 1024 and m or M = xx * 1024 * 1024
 */

static void parse_larsize(char *str)
{
	char *p = NULL;
	unsigned int size = strtoul(str, &p, 0);

	if (p != NULL) {
		if (*p == 'k' || *p == 'K')
			size *= 1024;
		else if (*p == 'm' || *p == 'M')
			size *= (1024 * 1024);
		else if (*p) {
			fprintf(stderr, "Unknown LAR size suffix '%c'\n", *p);
			exit(1);
		}
	}

	larsize = size;
}

int verbose(void)
{
	return isverbose;
}

long get_larsize(void)
{
	return larsize;
}

char *get_bootblock(void)
{
	return bootblock;
}

int create_lar(const char *archivename, struct file *files)
{
	struct lar *lar = lar_new_archive(archivename, larsize);

	if (lar == NULL) {
		fprintf(stderr, "Unable to create %s as a LAR archive.\n",
			archivename);
		exit(1);
	}

	for( ; files; files = files->next) {
		if (lar_add_file(lar, files)) {
			fprintf(stderr, "Error adding %s:%s (%d) to the LAR.\n",
				 files->name, files->pathname, files->algo);
			lar_close_archive(lar);
			exit(1);
		}
	}

	if (lar_add_bootblock(lar, bootblock)) {
		fprintf(stderr, "Error adding the bootblock to the LAR.\n");
		lar_close_archive(lar);
		exit(1);
	}

	lar_close_archive(lar);
	return 0;
}

int add_lar(const char *archivename, struct file *files)
{
	struct lar *lar = lar_open_archive(archivename);

	if (lar == NULL) {
		fprintf(stderr, "Unable to open LAR archive %s\n", archivename);
		exit(1);
	}

	for( ; files; files = files->next) {
		if (lar_add_file(lar, files)) {
			fprintf(stderr, "Error adding %s:%s (%d) to the LAR.\n",
				 files->name, files->pathname, files->algo);
			lar_close_archive(lar);
			exit(1);
		}
	}

	lar_close_archive(lar);
	return 0;
}

int list_lar(const char *archivename, struct file *files)
{
	struct lar *lar = lar_open_archive(archivename);

	if (lar == NULL) {
		fprintf(stderr, "Unable to open LAR archive %s\n", archivename);
		exit(1);
	}

	lar_list_files(lar, files);
	lar_close_archive(lar);
	return 0;
}

int zerofill_lar(const char *archivename)
{
	struct lar_header *header;
	int ret, hlen;
	int pathlen;
	u32 *walk,  csum;
	u32 offset;
	char *name = "zerofill";
	int zerolen;
	char *zero;

	struct lar *lar = lar_open_archive(archivename);

	if (lar == NULL) {
		fprintf(stderr, "Unable to open LAR archive %s\n", archivename);
		exit(1);
	}

	zerolen = maxsize(lar, name);
	if (zerolen <= 0) {
		fprintf(stderr, "No room for zerofill.\n");
		return -1;
	}

	zero = malloc(zerolen);
	memset(zero, 0xff, zerolen);

	lar_add_entry(lar, name, zero, zerolen, zerolen,0, 0, 0);

	lar_close_archive(lar);
	return 0;
}

int extract_lar(const char *archivename, struct file *files)
{
	int ret;

	struct lar *lar = lar_open_archive(archivename);

	if (lar == NULL) {
		fprintf(stderr, "Unable to open LAR archive %s\n", archivename);
		exit(1);
	}

	ret = lar_extract_files(lar, files);
	lar_close_archive(lar);
	return ret;
}

int main(int argc, char *argv[])
{
	int opt;
	int option_index = 0;

	int larmode = NONE;

	char *archivename = NULL;

	static struct option long_options[] = {
		{"add", 0, 0, 'a'},
		{"create", 0, 0, 'c'},
		{"compress-algo", 1, 0, 'C'},
		{"extract", 0, 0, 'x'},
		{"list", 0, 0, 'l'},
		{"size", 1, 0, 's'},
		{"bootblock", 1, 0, 'b'},
		{"elfparse", 1, 0, 'e'},
		{"verbose", 0, 0, 'v'},
		{"version", 0, 0, 'V'},
		{"zerofill", 0, 0, 'z'},
		{"help", 0, 0, 'h'},
		{0, 0, 0, 0}
	};

	if (argc < 3) {
		usage(argv[0]);
		exit(1);
	}

	while ((opt = getopt_long(argc, argv, "acC:xzels:b:vVh?",
				  long_options, &option_index)) != EOF) {
		switch (opt) {
		case 'a':
			larmode  = ADD;
			break;
		case 'c':
			larmode = CREATE;
			break;
		case 'C':
			if (strcasecmp("lzma", optarg) == 0) {
				algo = ALGO_LZMA;
			}
			if (strcasecmp("nrv2b", optarg) == 0) {
				algo = ALGO_NRV2B;
			}
			break;
		case 'l':
			larmode = LIST;
			break;
		case 'e':
			iselfparse = 1;
			break;
		case 'x':
			larmode = EXTRACT;
			break;
		case 'z':
			larmode = ZEROFILL;
			break;
		case 's':
			parse_larsize(optarg);
			break;
		case 'b':
			bootblock = strdup(optarg);
			if (!bootblock) {
				fprintf(stderr, "Out of memory.\n");
				exit(1);
			}
			break;
		case 'v':
			isverbose = 1;
			break;
		case 'V':
			printf("LAR - the Lightweight Archiver " VERSION "\n");
			break;
		default:
			usage(argv[0]);
			exit(1);
		}
	}

	// tar compatibility: Allow lar x as alternative to
	// lar -x.  This should be dropped or done correctly.
	// Right now, you'd have to write lar x -v instead of
	// lar xv... but the author of this software was too
	// lazy to handle all option parameter twice.
	if (larmode == NONE && optind < argc) {
		if (strncmp(argv[optind], "x", 2) == 0)
			larmode = EXTRACT;
		else if (strncmp(argv[optind], "c", 2) == 0)
			larmode = CREATE;
		else if (strncmp(argv[optind], "l", 2) == 0)
			larmode = LIST;
		else if (strncmp(argv[optind], "z", 2) == 0)
			larmode = ZEROFILL;

		/* If larmode changed in this if branch,
		 * eat a parameter
		 */
		if (larmode != NONE)
			optind++;
	}

	if (larmode == NONE) {
		usage(argv[0]);
		fprintf(stderr, "Error: No mode specified.\n\n");
		exit(1);
	}

	/* size only makes sense when creating a lar */
	if (larmode != CREATE && larsize) {
		printf("Warning: size parameter ignored since "
		       "not creating an archive.\n");
	}

	if (bootblock) {

		/* adding a bootblock only makes sense when creating a lar */
		if (larmode != CREATE) {
			printf("Warning: bootblock parameter ignored since "
			       "not creating an archive.\n");
		}

		/* adding a bootblock only makes sense when creating a lar */
		if (!larsize) {
			printf("When creating a LAR archive, you must specify an archive size.\n");
			exit(1);
		}
	}

	if (optind < argc) {
		archivename = argv[optind++];
	} else if (larmode != ZEROFILL) {

		usage(argv[0]);
		fprintf(stderr, "Error: No archive name.\n\n");
		exit(1);
	}

	/* when a new archive is created or added to, recurse over
	 * the physical files when a directory is found.
	 * Otherwise just add the directory to the match list
	 */

	while (optind < argc) {
		if (larmode == CREATE || larmode == ADD) {
			char *name=argv[optind++], *filename, *pathname;
			enum compalgo file_algo=algo;
			if (lar_process_name(name, &filename,
						&pathname, &file_algo))
				exit(1);
			add_files(filename,pathname, file_algo);
		} else
			add_file_or_directory(argv[optind++]);
	}

	switch (larmode) {
	case ADD:
		add_lar(archivename, get_files());
		break;
	case EXTRACT:
		extract_lar(archivename, get_files());
		break;
	case CREATE:
		create_lar(archivename, get_files());
		break;
	case LIST:
		list_lar(archivename, get_files());
		break;
	case ZEROFILL:
		zerofill_lar(archivename);
		break;
	}

	free_files();
	return 0;
}

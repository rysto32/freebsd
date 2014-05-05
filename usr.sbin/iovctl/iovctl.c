/*-
 * Copyright (c) 2013 Sandvine Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/iov.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <nv.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "iovctl.h"

static void	config_action(const char *filename, int dryrun);
static void	delete_action(const char *device, int dryrun);

/*
 * Fetch the config schema from the kernel via ioctl.  This function has to
 * call the ioctl twice: the first returns the amount of memory that we need
 * to allocate for the schema, and the second actually fetchs the schema.
 */
static nvlist_t *
get_schema(int fd)
{
	struct pci_iov_schema arg;
	nvlist_t *schema;
	int error;

	/* Do the ioctl() once to fetch the size of the schema. */
	arg.schema = NULL;
	arg.len = 0;
	error = ioctl(fd, IOV_GET_SCHEMA, &arg);
	if (error != 0)
		err(1, "Could not fetch size of config schema");

	arg.schema = malloc(arg.len);
	if (arg.schema == NULL)
		err(1, "Could not allocate %zu bytes for schema",
		    arg.len);

	/* Now do the ioctl() for real to get the schema. */
	error = ioctl(fd, IOV_GET_SCHEMA, &arg);
	if (error != 0 || arg.error != 0) {
		if (arg.error != 0)
			errno = arg.error;
		err(1, "Could not fetch config schema");
	}

	schema = nvlist_unpack(arg.schema, arg.len);
	if (schema == NULL)
		err(1, "Could not unpack schema");

	free(arg.schema);
	return (schema);
}

/*
 * Call the ioctl that activates SR-IOV and creates the VFs.
 */
static void
config_iov(int fd, const nvlist_t *config)
{
	struct pci_iov_arg arg;
	int error;

	arg.config = nvlist_pack(config, &arg.len);
	if (arg.config == NULL)
		err(1, "Could not pack configuration");

	error = ioctl(fd, IOV_CONFIG, &arg);
	if (error != 0)
		err(1, "Failed to configure SR-IOV");

	free(arg.config);
}

static void
usage(void)
{

	warnx("Usage: iovctl -f <config file> [-n]\n");
	warnx("       iovctl -D <PF device>\n");
	exit(1);

}

enum main_action {
	NONE,
	CONFIG,
	DELETE,
};

int
main(int argc, char **argv)
{
	const char *filename;
	int ch, dryrun;
	enum main_action action;

	filename = NULL;
	dryrun = 0;
	action = NONE;

	while ((ch = getopt(argc, argv, "D:f:n")) != -1) {
		switch (ch) {
		case 'D':
			filename = optarg;
			if (action != NONE) {
				warnx("-D flag is incompatible with -f flag\n");
				usage();
			}
			action = DELETE;
			break;
		case 'f':
			filename = optarg;
			if (action != NONE) {
				warnx("-f flag is incompatible with -D flag\n");
				usage();
			}
			action = CONFIG;
			break;
		case 'n':
			dryrun = 1;
			break;
		case '?':
			warnx("Unrecognized argument '-%c'\n", optopt);
			usage();
			break;
		}
	}

	switch (action) {
	case CONFIG:
		config_action(filename, dryrun);
		break;
	case DELETE:
		delete_action(filename, dryrun);
		break;
	default:
		usage();
		break;
	}

	exit(0);
}

static void
config_action(const char *filename, int dryrun)
{
	char *dev;
	nvlist_t *schema, *config;
	int fd;

	dev = find_device(filename);
	fd = open(dev, O_RDWR);
	if (fd < 0)
		err(1, "Could not open device '%s'", dev);

	schema = get_schema(fd);
	config = parse_config_file(filename, schema);
	if (config == NULL)
		errx(1, "Could not parse config");

	if (dryrun) {
		fprintf(stdout, "Schema:\n");
		nvlist_fdump(schema, stdout);
		fprintf(stdout, "\nConfig:\n");
		nvlist_fdump(config, stdout);
	} else
		config_iov(fd, config);

	nvlist_destroy(config);
	nvlist_destroy(schema);
	free(dev);
	close(fd);
}

static void
delete_action(const char *dev_name, int dryrun)
{
	char *dev;
	int fd, error;
	size_t copied, size;
	long path_max;

	if (dryrun != 0)
		errx(1, "-n option is not compatible with -D");

	path_max = pathconf("/dev", _PC_PATH_MAX);
	if (path_max < 0)
		err(1, "Could not get maximum path length");

	size = path_max;
	dev = malloc(size);
	if (dev_name[0] == '/')
		copied = strlcpy(dev, dev_name, size);
	else
		copied = snprintf(dev, size, "/dev/iov/%s", dev_name);

	/* >= to account for null terminator. */
	if (copied >= size)
		errx(1, "Provided file name too long");

	fd = open(dev, O_RDWR);
	if (fd < 0)
		err(1, "Could not open device '%s'", dev);

	error = ioctl(fd, IOV_DELETE);
	if (error != 0)
		err(1, "Failed to delete VFs");

	free(dev);
	close(fd);
}


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
#include <sysexits.h>
#include <unistd.h>

#include "iovctl.h"

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
		err(EX_OSERR, "Could not fetch size of config schema");

	arg.schema = malloc(arg.len);
	if (arg.schema == NULL)
		err(EX_OSERR, "Could not allocate %zu bytes for schema",
		    arg.len);

	/* Now do the ioctl() for real to get the schema. */
	error = ioctl(fd, IOV_GET_SCHEMA, &arg);
	if (error != 0 || arg.error != 0) {
		if (arg.error != 0)
			errno = arg.error;
		err(EX_OSERR, "Could not fetch config schema");
	}

	schema = nvlist_unpack(arg.schema, arg.len);
	if (schema == NULL)
		err(EX_OSERR, "Could not unpack schema");

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
		err(EX_OSERR, "Could not pack configuration");

	error = ioctl(fd, IOV_CONFIG, &arg);
	if (error != 0)
		err(EX_OSERR, "Failed to configure SR-IOV");

	free(arg.config);
}

static void
usage(void)
{

	fprintf(stderr, "Usage: iovctl -f <config file>\n");
	exit(EX_USAGE);
}

int
main(int argc, char **argv)
{
	const char *filename;
	char *dev;
	nvlist_t *schema, *config;
	int fd, ch, dryrun;

	filename = NULL;
	dryrun = 0;

	while ((ch = getopt(argc, argv, "f:n")) != -1) {
		switch (ch) {
		case 'f':
			filename = optarg;
			break;
		case 'n':
			dryrun = 1;
			break;
		case '?':
			fprintf(stderr, "Unrecognized argument '-%c'\n", optopt);
			usage();
			break;
		}
	}

	if (filename == NULL) {
		fprintf(stderr, "-f option is mandatory\n");
		usage();
	}

	dev = find_device(filename);
	fd = open(dev, O_RDWR);
	if (fd < 0)
		errx(EX_DATAERR, "Could not open device '%s'", dev);

	schema = get_schema(fd);
	config = parse_config_file(filename, schema);
	if (config == NULL)
		errx(EX_SOFTWARE, "Could not parse config");

	if (dryrun) {
		fprintf(stderr, "Schema:\n");
		nvlist_fdump(schema, stderr);
		fprintf(stderr, "\nConfig:\n");
		nvlist_fdump(config, stderr);
	} else
		config_iov(fd, config);

	nvlist_destroy(config);
	nvlist_destroy(schema);
	free(dev);

	exit(0);
}

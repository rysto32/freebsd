/*-
 * Copyright (c) 2009-2013 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Pawel Jakub Dawidek under sponsorship from
 * the FreeBSD Foundation.
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
 */

#include <sys/types.h>
#include <sys/endian.h>
#include <sys/queue.h>
#include <sys/socket.h>

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#define	_WITH_DPRINTF
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef HAVE_PJDLOG
#include <pjdlog.h>
#endif

#include "common_impl.h"
#include "dnv.h"
#include "msgio.h"
#include "nv.h"
#include "nv_impl.h"
#include "nvlist_getters.h"
#include "nvlist_impl.h"
#include "nvpair_impl.h"

#ifndef	HAVE_PJDLOG
#include <assert.h>
#define	PJDLOG_ASSERT(...)		assert(__VA_ARGS__)
#define	PJDLOG_RASSERT(expr, ...)	assert(expr)
#define	PJDLOG_ABORT(...)		do {				\
	fprintf(stderr, "%s:%u: ", __FILE__, __LINE__);			\
	fprintf(stderr, __VA_ARGS__);					\
	fprintf(stderr, "\n");						\
	abort();							\
} while (0)
#endif

/*
 * Dump content of nvlist.
 */
static void
nvlist_xdump(const nvlist_t *nvl, int fd, int level)
{
	nvpair_t *nvp;

	PJDLOG_ASSERT(level < NVLIST_MAX_LEVEL);

	if (nvlist_error(nvl) != 0) {
		dprintf(fd, "%*serror: %d\n", level * 4, "",
		    nvlist_error(nvl));
		return;
	}

	for (nvp = nvlist_first_nvpair(nvl); nvp != NULL;
	    nvp = nvlist_next_nvpair(nvl, nvp)) {
		dprintf(fd, "%*s%s (%s):", level * 4, "", nvpair_name(nvp),
		    nvpair_type_string(nvpair_type(nvp)));
		switch (nvpair_type(nvp)) {
		case NV_TYPE_NULL:
			dprintf(fd, " null\n");
			break;
		case NV_TYPE_BOOL:
			dprintf(fd, " %s\n", nvpair_get_bool(nvp) ?
			    "TRUE" : "FALSE");
			break;
		case NV_TYPE_NUMBER:
			dprintf(fd, " %ju (%jd) (0x%jx)\n",
			    (uintmax_t)nvpair_get_number(nvp),
			    (intmax_t)nvpair_get_number(nvp),
			    (uintmax_t)nvpair_get_number(nvp));
			break;
		case NV_TYPE_STRING:
			dprintf(fd, " [%s]\n", nvpair_get_string(nvp));
			break;
		case NV_TYPE_NVLIST:
			dprintf(fd, "\n");
			nvlist_xdump(nvpair_get_nvlist(nvp), fd, level + 1);
			break;
		case NV_TYPE_DESCRIPTOR:
			dprintf(fd, " %d\n", nvpair_get_descriptor(nvp));
			break;
		case NV_TYPE_BINARY:
		    {
			const unsigned char *binary;
			unsigned int ii;
			size_t size;

			binary = nvpair_get_binary(nvp, &size);
			dprintf(fd, " %zu ", size);
			for (ii = 0; ii < size; ii++)
				dprintf(fd, "%02hhx", binary[ii]);
			dprintf(fd, "\n");
			break;
		    }
		default:
			PJDLOG_ABORT("Unknown type: %d.", nvpair_type(nvp));
		}
	}
}

void
nvlist_dump(const nvlist_t *nvl, int fd)
{

	nvlist_xdump(nvl, fd, 0);
}

void
nvlist_fdump(const nvlist_t *nvl, FILE *fp)
{

	fflush(fp);
	nvlist_dump(nvl, fileno(fp));
}

NVLIST_EXISTS(descriptor, DESCRIPTOR)
NVLIST_EXISTSF(descriptor)
NVLIST_EXISTSV(descriptor)

void
nvlist_add_descriptor(nvlist_t *nvl, const char *name, int value)
{

	nvlist_addf_descriptor(nvl, value, "%s", name);
}

void
nvlist_addf_descriptor(nvlist_t *nvl, int value, const char *namefmt, ...)
{
	va_list nameap;

	va_start(nameap, namefmt);
	nvlist_addv_descriptor(nvl, value, namefmt, nameap);
	va_end(nameap);
}

void
nvlist_addv_descriptor(nvlist_t *nvl, int value, const char *namefmt,
    va_list nameap)
{
	nvpair_t *nvp;

	if (nvlist_error(nvl) != 0) {
		errno = nvlist_error(nvl);
		return;
	}

	nvp = nvpair_createv_descriptor(value, namefmt, nameap);
	if (nvp == NULL)
		nvl->nvl_error = errno = (errno != 0 ? errno : ENOMEM);
	else
		nvlist_move_nvpair(nvl, nvp);
}

void
nvlist_movev_descriptor(nvlist_t *nvl, int value, const char *namefmt,
    va_list nameap)
{
	nvpair_t *nvp;

	if (nvlist_error(nvl) != 0) {
		close(value);
		errno = nvlist_error(nvl);
		return;
	}

	nvp = nvpair_movev_descriptor(value, namefmt, nameap);
	if (nvp == NULL)
		nvl->nvl_error = errno = (errno != 0 ? errno : ENOMEM);
	else
		nvlist_move_nvpair(nvl, nvp);
}

NVLIST_MOVE(int, descriptor)
NVLIST_MOVEF(int, descriptor)
NVLIST_GET(int, descriptor, DESCRIPTOR)
NVLIST_GETF(int, descriptor)
NVLIST_GETV(int, descriptor, DESCRIPTOR)
NVLIST_TAKE(int, descriptor, DESCRIPTOR)
NVLIST_TAKEF(int, descriptor)
NVLIST_TAKEV(int, descriptor, DESCRIPTOR)
NVLIST_FREE(descriptor, DESCRIPTOR)
NVLIST_FREEF(descriptor)
NVLIST_FREEV(descriptor, DESCRIPTOR)

int
nvlist_send(int sock, const nvlist_t *nvl)
{
	size_t datasize, nfds;
	int *fds;
	void *data;
	int64_t fdidx;
	int serrno, ret;

	if (nvlist_error(nvl) != 0) {
		errno = nvlist_error(nvl);
		return (-1);
	}

	fds = nvlist_descriptors(nvl, &nfds);
	if (fds == NULL)
		return (-1);

	ret = -1;
	data = NULL;
	fdidx = 0;

	data = nvlist_xpack(nvl, &fdidx, &datasize);
	if (data == NULL)
		goto out;

	if (buf_send(sock, data, datasize) == -1)
		goto out;

	if (nfds > 0) {
		if (fd_send(sock, fds, nfds) == -1)
			goto out;
	}

	ret = 0;
out:
	serrno = errno;
	free(fds);
	free(data);
	errno = serrno;
	return (ret);
}

nvlist_t *
nvlist_recv(int sock)
{
	struct nvlist_header nvlhdr;
	nvlist_t *nvl, *ret;
	unsigned char *buf;
	size_t nfds, size;
	int serrno, *fds;

	if (buf_recv(sock, &nvlhdr, sizeof(nvlhdr)) == -1)
		return (NULL);

	if (!nvlist_check_header(&nvlhdr))
		return (NULL);

	nfds = (size_t)nvlhdr.nvlh_descriptors;
	size = sizeof(nvlhdr) + (size_t)nvlhdr.nvlh_size;

	buf = malloc(size);
	if (buf == NULL)
		return (NULL);

	memcpy(buf, &nvlhdr, sizeof(nvlhdr));

	ret = NULL;
	fds = NULL;

	if (buf_recv(sock, buf + sizeof(nvlhdr), size - sizeof(nvlhdr)) == -1)
		goto out;

	if (nfds > 0) {
		fds = malloc(nfds * sizeof(fds[0]));
		if (fds == NULL)
			goto out;
		if (fd_recv(sock, fds, nfds) == -1)
			goto out;
	}

	nvl = nvlist_xunpack(buf, size, fds, nfds, 0);
	if (nvl == NULL)
		goto out;

	ret = nvl;
out:
	serrno = errno;
	free(buf);
	free(fds);
	errno = serrno;

	return (ret);
}

nvlist_t *
nvlist_xfer(int sock, nvlist_t *nvl)
{

	if (nvlist_send(sock, nvl) < 0) {
		nvlist_destroy(nvl);
		return (NULL);
	}
	nvlist_destroy(nvl);
	return (nvlist_recv(sock));
}

nvpair_t *
nvpair_createv_descriptor(int value, const char *namefmt, va_list nameap)
{
	nvpair_t *nvp;

	if (value < 0 || !fd_is_valid(value)) {
		errno = EBADF;
		return (NULL);
	}

	value = fcntl(value, F_DUPFD_CLOEXEC, 0);
	if (value < 0)
		return (NULL);

	nvp = nvpair_allocv(NV_TYPE_DESCRIPTOR, (uint64_t)value,
	    sizeof(int64_t), namefmt, nameap);
	if (nvp == NULL)
		close(value);

	return (nvp);
}

unsigned char *
nvpair_pack_descriptor(const nvpair_t *nvp, unsigned char *ptr, int64_t *fdidxp,
    size_t *leftp)
{
	int64_t value;

	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvp->nvp_type == NV_TYPE_DESCRIPTOR);

	value = (int64_t)nvp->nvp_data;
	if (value != -1) {
		/*
		 * If there is a real descriptor here, we change its number
		 * to position in the array of descriptors send via control
		 * message.
		 */
		PJDLOG_ASSERT(fdidxp != NULL);

		value = *fdidxp;
		(*fdidxp)++;
	}

	PJDLOG_ASSERT(*leftp >= sizeof(value));
	memcpy(ptr, &value, sizeof(value));
	ptr += sizeof(value);
	*leftp -= sizeof(value);

	return (ptr);
}

const unsigned char *
nvpair_unpack_descriptor(int flags, nvpair_t *nvp, const unsigned char *ptr,
    size_t *leftp, const int *fds, size_t nfds)
{
	int64_t idx;

	PJDLOG_ASSERT(nvp->nvp_type == NV_TYPE_DESCRIPTOR);

	if (nvp->nvp_datasize != sizeof(idx)) {
		errno = EINVAL;
		return (NULL);
	}
	if (*leftp < sizeof(idx)) {
		errno = EINVAL;
		return (NULL);
	}

	if ((flags & NV_FLAG_BIG_ENDIAN) != 0)
		idx = be64dec(ptr);
	else
		idx = le64dec(ptr);

	if (idx < 0) {
		errno = EINVAL;
		return (NULL);
	}

	if ((size_t)idx >= nfds) {
		errno = EINVAL;
		return (NULL);
	}

	nvp->nvp_data = (uint64_t)fds[idx];

	ptr += sizeof(idx);
	*leftp -= sizeof(idx);

	return (ptr);
}

nvpair_t *
nvpair_create_descriptor(const char *name, int value)
{

	return (nvpair_createf_descriptor(value, "%s", name));
}

nvpair_t *
nvpair_createf_descriptor(int value, const char *namefmt, ...)
{
	va_list nameap;
	nvpair_t *nvp;

	va_start(nameap, namefmt);
	nvp = nvpair_createv_descriptor(value, namefmt, nameap);
	va_end(nameap);

	return (nvp);
}

nvpair_t *
nvpair_move_descriptor(const char *name, int value)
{

	return (nvpair_movef_descriptor(value, "%s", name));
}

nvpair_t *
nvpair_movef_descriptor(int value, const char *namefmt, ...)
{
	va_list nameap;
	nvpair_t *nvp;

	va_start(nameap, namefmt);
	nvp = nvpair_movev_descriptor(value, namefmt, nameap);
	va_end(nameap);

	return (nvp);
}

nvpair_t *
nvpair_movev_descriptor(int value, const char *namefmt, va_list nameap)
{

	if (value < 0 || !fd_is_valid(value)) {
		errno = EBADF;
		return (NULL);
	}
	
	return (nvpair_allocv(NV_TYPE_DESCRIPTOR, (uint64_t)value,
	    sizeof(int64_t), namefmt, nameap));
}

int
nvpair_get_descriptor(const nvpair_t *nvp)
{

	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvp->nvp_type == NV_TYPE_DESCRIPTOR);

	return ((int)nvp->nvp_data);
}


DNVLIST_GET(int, descriptor)
DNVLIST_GETF(int, descriptor)
DNVLIST_GETV(int, descriptor)
DNVLIST_TAKE(int, descriptor)
DNVLIST_TAKEF(int, descriptor)
DNVLIST_TAKEV(int, descriptor)

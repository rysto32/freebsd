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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/queue.h>
#include <sys/socket.h>

#include <errno.h>
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

#define	NV_FLAG_PRIVATE_MASK	(NV_FLAG_BIG_ENDIAN)
#define	NV_FLAG_PUBLIC_MASK	(NV_FLAG_IGNORE_CASE)
#define	NV_FLAG_ALL_MASK	(NV_FLAG_PRIVATE_MASK | NV_FLAG_PUBLIC_MASK)

#define	NVLIST_ASSERT(nvl)	do {					\
	PJDLOG_ASSERT((nvl) != NULL);					\
	PJDLOG_ASSERT((nvl)->nvl_magic == NVLIST_MAGIC);		\
} while (0)


nvlist_t *
nvlist_create(int flags)
{
	nvlist_t *nvl;

	PJDLOG_ASSERT((flags & ~(NV_FLAG_PUBLIC_MASK)) == 0);

	nvl = malloc(sizeof(*nvl));
	nvl->nvl_error = 0;
	nvl->nvl_flags = flags;
	nvl->nvl_parent = NULL;
	TAILQ_INIT(&nvl->nvl_head);
	nvl->nvl_magic = NVLIST_MAGIC;

	return (nvl);
}

void
nvlist_destroy(nvlist_t *nvl)
{
	nvpair_t *nvp;
	int serrno;

	if (nvl == NULL)
		return;

	serrno = errno;

	NVLIST_ASSERT(nvl);

	while ((nvp = nvlist_first_nvpair(nvl)) != NULL) {
		nvlist_remove_nvpair(nvl, nvp);
		nvpair_free(nvp);
	}
	nvl->nvl_magic = 0;
	free(nvl);

	errno = serrno;
}

void
nvlist_set_error(nvlist_t *nvl, int error)
{

	if (nvl != NULL && error != 0 && nvl->nvl_error == 0)
		nvl->nvl_error = error;
}

int
nvlist_error(const nvlist_t *nvl)
{

	if (nvl == NULL)
		return (ENOMEM);

	NVLIST_ASSERT(nvl);

	return (nvl->nvl_error);
}

nvpair_t *
nvlist_get_nvpair_parent(const nvlist_t *nvl)
{

	NVLIST_ASSERT(nvl);

	return (nvl->nvl_parent);
}

const nvlist_t *
nvlist_get_parent(const nvlist_t *nvl)
{

	NVLIST_ASSERT(nvl);

	if (nvl->nvl_parent == NULL)
		return (NULL);

	return (nvpair_nvlist(nvl->nvl_parent));
}

void
nvlist_set_parent(nvlist_t *nvl, nvpair_t *parent)
{

	NVLIST_ASSERT(nvl);

	nvl->nvl_parent = parent;
}

bool
nvlist_empty(const nvlist_t *nvl)
{

	NVLIST_ASSERT(nvl);
	PJDLOG_ASSERT(nvl->nvl_error == 0);

	return (nvlist_first_nvpair(nvl) == NULL);
}

void
nvlist_report_missing(int type, const char *name)
{

	PJDLOG_ABORT("Element '%s' of type %s doesn't exist.",
	    name != NULL ? name : "N/A", nvpair_type_string(type));
}

nvpair_t *
nvlist_find(const nvlist_t *nvl, int type, const char *name)
{
	nvpair_t *nvp;

	NVLIST_ASSERT(nvl);
	PJDLOG_ASSERT(nvl->nvl_error == 0);
	PJDLOG_ASSERT(type == NV_TYPE_NONE ||
	    (type >= NV_TYPE_FIRST && type <= NV_TYPE_LAST));

	for (nvp = nvlist_first_nvpair(nvl); nvp != NULL;
	    nvp = nvlist_next_nvpair(nvl, nvp)) {
		if (type != NV_TYPE_NONE && nvpair_type(nvp) != type)
			continue;
		if ((nvl->nvl_flags & NV_FLAG_IGNORE_CASE) != 0) {
			if (strcasecmp(nvpair_name(nvp), name) != 0)
				continue;
		} else {
			if (strcmp(nvpair_name(nvp), name) != 0)
				continue;
		}
		break;
	}

	if (nvp == NULL)
		errno = ENOENT;

	return (nvp);
}

bool
nvlist_exists_type(const nvlist_t *nvl, const char *name, int type)
{

	NVLIST_ASSERT(nvl);
	PJDLOG_ASSERT(nvl->nvl_error == 0);
	PJDLOG_ASSERT(type == NV_TYPE_NONE ||
	    (type >= NV_TYPE_FIRST && type <= NV_TYPE_LAST));

	return (nvlist_find(nvl, type, name) != NULL);
}

bool
nvlist_existsf_type(const nvlist_t *nvl, int type, const char *namefmt, ...)
{
	va_list nameap;
	bool ret;

	va_start(nameap, namefmt);
	ret = nvlist_existsv_type(nvl, type, namefmt, nameap);
	va_end(nameap);

	return (ret);
}

bool
nvlist_existsv_type(const nvlist_t *nvl, int type, const char *namefmt,
    va_list nameap)
{
	char *name;
	bool exists;

	vasprintf(&name, namefmt, nameap);
	if (name == NULL)
		return (0);

	exists = nvlist_exists_type(nvl, name, type);
	free(name);
	return (exists);
}

void
nvlist_free_type(nvlist_t *nvl, const char *name, int type)
{
	nvpair_t *nvp;

	NVLIST_ASSERT(nvl);
	PJDLOG_ASSERT(nvl->nvl_error == 0);
	PJDLOG_ASSERT(type == NV_TYPE_NONE ||
	    (type >= NV_TYPE_FIRST && type <= NV_TYPE_LAST));

	nvp = nvlist_find(nvl, type, name);
	if (nvp != NULL)
		nvlist_free_nvpair(nvl, nvp);
	else
		nvlist_report_missing(type, name);
}

void
nvlist_freef_type(nvlist_t *nvl, int type, const char *namefmt, ...)
{
	va_list nameap;

	va_start(nameap, namefmt);
	nvlist_freev_type(nvl, type, namefmt, nameap);
	va_end(nameap);
}

void
nvlist_freev_type(nvlist_t *nvl, int type, const char *namefmt, va_list nameap)
{
	char *name;

	vasprintf(&name, namefmt, nameap);
	if (name == NULL)
		nvlist_report_missing(type, "<unknown>");
	nvlist_free_type(nvl, name, type);
	free(name);
}

nvlist_t *
nvlist_clone(const nvlist_t *nvl)
{
	nvlist_t *newnvl;
	nvpair_t *nvp, *newnvp;

	NVLIST_ASSERT(nvl);

	if (nvl->nvl_error != 0) {
		errno = nvl->nvl_error;
		return (NULL);
	}

	newnvl = nvlist_create(nvl->nvl_flags & NV_FLAG_PUBLIC_MASK);
	for (nvp = nvlist_first_nvpair(nvl); nvp != NULL;
	    nvp = nvlist_next_nvpair(nvl, nvp)) {
		newnvp = nvpair_clone(nvp);
		if (newnvp == NULL)
			break;
		nvlist_move_nvpair(newnvl, newnvp);
	}
	if (nvp != NULL) {
		nvlist_destroy(newnvl);
		return (NULL);
	}
	return (newnvl);
}

/*
 * The function obtains size of the nvlist after nvlist_pack().
 */
size_t
nvlist_size(const nvlist_t *nvl)
{
	const nvpair_t *nvp;
	size_t size;

	NVLIST_ASSERT(nvl);
	PJDLOG_ASSERT(nvl->nvl_error == 0);

	size = sizeof(struct nvlist_header);
	nvp = nvlist_first_nvpair(nvl);
	while (nvp != NULL) {
		size += nvpair_header_size();
		size += strlen(nvpair_name(nvp)) + 1;
		if (nvpair_type(nvp) == NV_TYPE_NVLIST) {
			size += sizeof(struct nvlist_header);
			size += nvpair_header_size() + 1;
			nvl = nvpair_get_nvlist(nvp);
			PJDLOG_ASSERT(nvl->nvl_error == 0);
			nvp = nvlist_first_nvpair(nvl);
			continue;
		} else {
			size += nvpair_size(nvp);
		}

		while ((nvp = nvlist_next_nvpair(nvl, nvp)) == NULL) {
			nvp = nvlist_get_nvpair_parent(nvl);
			if (nvp == NULL)
				goto out;
			nvl = nvlist_get_parent(nvl);
		}
	}

out:
	return (size);
}

static int *
nvlist_xdescriptors(const nvlist_t *nvl, int *descs, int level)
{
	const nvpair_t *nvp;

	NVLIST_ASSERT(nvl);
	PJDLOG_ASSERT(nvl->nvl_error == 0);
	PJDLOG_ASSERT(level < 3);

	for (nvp = nvlist_first_nvpair(nvl); nvp != NULL;
	    nvp = nvlist_next_nvpair(nvl, nvp)) {
		switch (nvpair_type(nvp)) {
		case NV_TYPE_DESCRIPTOR:
			*descs = nvpair_get_descriptor(nvp);
			descs++;
			break;
		case NV_TYPE_NVLIST:
			descs = nvlist_xdescriptors(nvpair_get_nvlist(nvp),
			    descs, level + 1);
			break;
		}
	}

	return (descs);
}

int *
nvlist_descriptors(const nvlist_t *nvl, size_t *nitemsp)
{
	size_t nitems;
	int *fds;

	nitems = nvlist_ndescriptors(nvl);
	fds = malloc(sizeof(fds[0]) * (nitems + 1));
	if (fds == NULL)
		return (NULL);
	if (nitems > 0)
		nvlist_xdescriptors(nvl, fds, 0);
	fds[nitems] = -1;
	if (nitemsp != NULL)
		*nitemsp = nitems;
	return (fds);
}

static size_t
nvlist_xndescriptors(const nvlist_t *nvl, int level)
{
	const nvpair_t *nvp;
	size_t ndescs;

	NVLIST_ASSERT(nvl);
	PJDLOG_ASSERT(nvl->nvl_error == 0);
	PJDLOG_ASSERT(level < 3);

	ndescs = 0;
	for (nvp = nvlist_first_nvpair(nvl); nvp != NULL;
	    nvp = nvlist_next_nvpair(nvl, nvp)) {
		switch (nvpair_type(nvp)) {
		case NV_TYPE_DESCRIPTOR:
			ndescs++;
			break;
		case NV_TYPE_NVLIST:
			ndescs += nvlist_xndescriptors(nvpair_get_nvlist(nvp),
			    level + 1);
			break;
		}
	}

	return (ndescs);
}

size_t
nvlist_ndescriptors(const nvlist_t *nvl)
{

	return (nvlist_xndescriptors(nvl, 0));
}

static unsigned char *
nvlist_pack_header(const nvlist_t *nvl, unsigned char *ptr, size_t *leftp)
{
	struct nvlist_header nvlhdr;

	NVLIST_ASSERT(nvl);

	nvlhdr.nvlh_magic = NVLIST_HEADER_MAGIC;
	nvlhdr.nvlh_version = NVLIST_HEADER_VERSION;
	nvlhdr.nvlh_flags = nvl->nvl_flags;
#if BYTE_ORDER == BIG_ENDIAN
	nvlhdr.nvlh_flags |= NV_FLAG_BIG_ENDIAN;
#endif
	nvlhdr.nvlh_descriptors = nvlist_ndescriptors(nvl);
	nvlhdr.nvlh_size = *leftp - sizeof(nvlhdr);
	PJDLOG_ASSERT(*leftp >= sizeof(nvlhdr));
	memcpy(ptr, &nvlhdr, sizeof(nvlhdr));
	ptr += sizeof(nvlhdr);
	*leftp -= sizeof(nvlhdr);

	return (ptr);
}

void *
nvlist_xpack(const nvlist_t *nvl, int64_t *fdidxp, size_t *sizep)
{
	unsigned char *buf, *ptr;
	size_t left, size;
	nvpair_t *nvp;

	NVLIST_ASSERT(nvl);

	if (nvl->nvl_error != 0) {
		errno = nvl->nvl_error;
		return (NULL);
	}

	size = nvlist_size(nvl);
	buf = malloc(size);
	if (buf == NULL)
		return (NULL);

	ptr = buf;
	left = size;

	ptr = nvlist_pack_header(nvl, ptr, &left);

	nvp = nvlist_first_nvpair(nvl);
	while (nvp != NULL) {
		NVPAIR_ASSERT(nvp);

		nvpair_init_datasize(nvp);
		ptr = nvpair_pack_header(nvp, ptr, &left);
		if (ptr == NULL) {
			free(buf);
			return (NULL);
		}
		switch (nvpair_type(nvp)) {
		case NV_TYPE_NULL:
			ptr = nvpair_pack_null(nvp, ptr, &left);
			break;
		case NV_TYPE_BOOL:
			ptr = nvpair_pack_bool(nvp, ptr, &left);
			break;
		case NV_TYPE_NUMBER:
			ptr = nvpair_pack_number(nvp, ptr, &left);
			break;
		case NV_TYPE_STRING:
			ptr = nvpair_pack_string(nvp, ptr, &left);
			break;
		case NV_TYPE_NVLIST:
			nvl = nvpair_get_nvlist(nvp);
			nvp = nvlist_first_nvpair(nvl);
			ptr = nvlist_pack_header(nvl, ptr, &left);
			continue;
		case NV_TYPE_DESCRIPTOR:
			ptr = nvpair_pack_descriptor(nvp, ptr, fdidxp, &left);
			break;
		case NV_TYPE_BINARY:
			ptr = nvpair_pack_binary(nvp, ptr, &left);
			break;
		default:
			PJDLOG_ABORT("Invalid type (%d).", nvpair_type(nvp));
		}
		if (ptr == NULL) {
			free(buf);
			return (NULL);
		}
		while ((nvp = nvlist_next_nvpair(nvl, nvp)) == NULL) {
			nvp = nvlist_get_nvpair_parent(nvl);
			if (nvp == NULL)
				goto out;
			ptr = nvpair_pack_nvlist_up(ptr, &left);
			if (ptr == NULL)
				goto out;
			nvl = nvlist_get_parent(nvl);
		}
	}

out:
	if (sizep != NULL)
		*sizep = size;
	return (buf);
}

void *
nvlist_pack(const nvlist_t *nvl, size_t *sizep)
{

	NVLIST_ASSERT(nvl);

	if (nvl->nvl_error != 0) {
		errno = nvl->nvl_error;
		return (NULL);
	}

	if (nvlist_ndescriptors(nvl) > 0) {
		errno = EOPNOTSUPP;
		return (NULL);
	}

	return (nvlist_xpack(nvl, NULL, sizep));
}

bool
nvlist_check_header(struct nvlist_header *nvlhdrp)
{

	if (nvlhdrp->nvlh_magic != NVLIST_HEADER_MAGIC) {
		errno = EINVAL;
		return (false);
	}
	if ((nvlhdrp->nvlh_flags & ~NV_FLAG_ALL_MASK) != 0) {
		errno = EINVAL;
		return (false);
	}
#if BYTE_ORDER == BIG_ENDIAN
	if ((nvlhdrp->nvlh_flags & NV_FLAG_BIG_ENDIAN) == 0) {
		nvlhdrp->nvlh_size = le64toh(nvlhdrp->nvlh_size);
		nvlhdrp->nvlh_descriptors = le64toh(nvlhdrp->nvlh_descriptors);
	}
#else
	if ((nvlhdrp->nvlh_flags & NV_FLAG_BIG_ENDIAN) != 0) {
		nvlhdrp->nvlh_size = be64toh(nvlhdrp->nvlh_size);
		nvlhdrp->nvlh_descriptors = be64toh(nvlhdrp->nvlh_descriptors);
	}
#endif
	return (true);
}

const unsigned char *
nvlist_unpack_header(nvlist_t *nvl, const unsigned char *ptr, size_t nfds,
    bool *isbep, size_t *leftp)
{
	struct nvlist_header nvlhdr;

	if (*leftp < sizeof(nvlhdr))
		goto failed;

	memcpy(&nvlhdr, ptr, sizeof(nvlhdr));

	if (!nvlist_check_header(&nvlhdr))
		goto failed;

	if (nvlhdr.nvlh_size != *leftp - sizeof(nvlhdr))
		goto failed;

	/*
	 * nvlh_descriptors might be smaller than nfds in embedded nvlists.
	 */
	if (nvlhdr.nvlh_descriptors > nfds)
		goto failed;

	if ((nvlhdr.nvlh_flags & ~NV_FLAG_ALL_MASK) != 0)
		goto failed;

	nvl->nvl_flags = (nvlhdr.nvlh_flags & NV_FLAG_PUBLIC_MASK);

	ptr += sizeof(nvlhdr);
	if (isbep != NULL)
		*isbep = (((int)nvlhdr.nvlh_flags & NV_FLAG_BIG_ENDIAN) != 0);
	*leftp -= sizeof(nvlhdr);

	return (ptr);
failed:
	errno = EINVAL;
	return (NULL);
}

nvlist_t *
nvlist_xunpack(const void *buf, size_t size, const int *fds, size_t nfds)
{
	const unsigned char *ptr;
	nvlist_t *nvl, *retnvl, *tmpnvl;
	nvpair_t *nvp;
	size_t left;
	bool isbe;

	left = size;
	ptr = buf;

	tmpnvl = NULL;
	nvl = retnvl = nvlist_create(0);
	if (nvl == NULL)
		goto failed;

	ptr = nvlist_unpack_header(nvl, ptr, nfds, &isbe, &left);
	if (ptr == NULL)
		goto failed;

	while (left > 0) {
		ptr = nvpair_unpack(isbe, ptr, &left, &nvp);
		if (ptr == NULL)
			goto failed;
		switch (nvpair_type(nvp)) {
		case NV_TYPE_NULL:
			ptr = nvpair_unpack_null(isbe, nvp, ptr, &left);
			break;
		case NV_TYPE_BOOL:
			ptr = nvpair_unpack_bool(isbe, nvp, ptr, &left);
			break;
		case NV_TYPE_NUMBER:
			ptr = nvpair_unpack_number(isbe, nvp, ptr, &left);
			break;
		case NV_TYPE_STRING:
			ptr = nvpair_unpack_string(isbe, nvp, ptr, &left);
			break;
		case NV_TYPE_NVLIST:
			ptr = nvpair_unpack_nvlist(isbe, nvp, ptr, &left, nfds,
			    &tmpnvl);
			nvlist_set_parent(tmpnvl, nvp);
			break;
		case NV_TYPE_DESCRIPTOR:
			ptr = nvpair_unpack_descriptor(isbe, nvp, ptr, &left,
			    fds, nfds);
			break;
		case NV_TYPE_BINARY:
			ptr = nvpair_unpack_binary(isbe, nvp, ptr, &left);
			break;
		case NV_TYPE_NVLIST_UP:
			if (nvl->nvl_parent == NULL)
				goto failed;
			nvl = nvpair_nvlist(nvl->nvl_parent);
			continue;
		default:
			PJDLOG_ABORT("Invalid type (%d).", nvpair_type(nvp));
		}
		if (ptr == NULL)
			goto failed;
		nvlist_move_nvpair(nvl, nvp);
		if (tmpnvl != NULL) {
			nvl = tmpnvl;
			tmpnvl = NULL;
		}
	}

	return (retnvl);
failed:
	nvlist_destroy(retnvl);
	return (NULL);
}

nvlist_t *
nvlist_unpack(const void *buf, size_t size)
{

	return (nvlist_xunpack(buf, size, NULL, 0));
}

nvpair_t *
nvlist_first_nvpair(const nvlist_t *nvl)
{

	NVLIST_ASSERT(nvl);

	return (TAILQ_FIRST(&nvl->nvl_head));
}

nvpair_t *
nvlist_next_nvpair(const nvlist_t *nvl, const nvpair_t *nvp)
{
	nvpair_t *retnvp;

	NVLIST_ASSERT(nvl);
	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvpair_nvlist(nvp) == nvl);

	retnvp = nvpair_next(nvp);
	PJDLOG_ASSERT(retnvp == NULL || nvpair_nvlist(retnvp) == nvl);

	return (retnvp);

}

nvpair_t *
nvlist_prev_nvpair(const nvlist_t *nvl, const nvpair_t *nvp)
{
	nvpair_t *retnvp;

	NVLIST_ASSERT(nvl);
	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvpair_nvlist(nvp) == nvl);

	retnvp = nvpair_prev(nvp);
	PJDLOG_ASSERT(nvpair_nvlist(retnvp) == nvl);

	return (retnvp);
}

const char *
nvlist_next(const nvlist_t *nvl, int *typep, void **cookiep)
{
	nvpair_t *nvp;

	NVLIST_ASSERT(nvl);
	PJDLOG_ASSERT(cookiep != NULL);

	if (*cookiep == NULL)
		nvp = nvlist_first_nvpair(nvl);
	else
		nvp = nvlist_next_nvpair(nvl, *cookiep);
	if (nvp == NULL)
		return (NULL);
	if (typep != NULL)
		*typep = nvpair_type(nvp);
	*cookiep = nvp;
	return (nvpair_name(nvp));
}

bool
nvlist_exists(const nvlist_t *nvl, const char *name)
{

	return (nvlist_find(nvl, NV_TYPE_NONE, name) != NULL);
}

NVLIST_EXISTS(null, NULL)
NVLIST_EXISTS(bool, BOOL)
NVLIST_EXISTS(number, NUMBER)
NVLIST_EXISTS(string, STRING)
NVLIST_EXISTS(nvlist, NVLIST)
NVLIST_EXISTS(binary, BINARY)

bool
nvlist_existsf(const nvlist_t *nvl, const char *namefmt, ...)
{
	va_list nameap;
	bool ret;

	va_start(nameap, namefmt);
	ret = nvlist_existsv(nvl, namefmt, nameap);
	va_end(nameap);
	return (ret);
}

NVLIST_EXISTSF(null)
NVLIST_EXISTSF(bool)
NVLIST_EXISTSF(number)
NVLIST_EXISTSF(string)
NVLIST_EXISTSF(nvlist)
NVLIST_EXISTSF(binary)

bool
nvlist_existsv(const nvlist_t *nvl, const char *namefmt, va_list nameap)
{
	char *name;
	bool exists;

	vasprintf(&name, namefmt, nameap);
	if (name == NULL)
		return (0);

	exists = nvlist_exists(nvl, name);
	free(name);
	return (exists);
}

NVLIST_EXISTSV(null)
NVLIST_EXISTSV(bool)
NVLIST_EXISTSV(number)
NVLIST_EXISTSV(string)
NVLIST_EXISTSV(nvlist)
NVLIST_EXISTSV(binary)

void
nvlist_add_null(nvlist_t *nvl, const char *name)
{

	nvlist_addf_null(nvl, "%s", name);
}

void
nvlist_add_bool(nvlist_t *nvl, const char *name, bool value)
{

	nvlist_addf_bool(nvl, value, "%s", name);
}

void
nvlist_add_number(nvlist_t *nvl, const char *name, uint64_t value)
{

	nvlist_addf_number(nvl, value, "%s", name);
}

void
nvlist_add_string(nvlist_t *nvl, const char *name, const char *value)
{

	nvlist_addf_string(nvl, value, "%s", name);
}

void
nvlist_add_stringf(nvlist_t *nvl, const char *name, const char *valuefmt, ...)
{
	va_list valueap;

	va_start(valueap, valuefmt);
	nvlist_add_stringv(nvl, name, valuefmt, valueap);
	va_end(valueap);
}

void
nvlist_add_stringv(nvlist_t *nvl, const char *name, const char *valuefmt,
    va_list valueap)
{
	nvpair_t *nvp;

	if (nvlist_error(nvl) != 0) {
		errno = nvlist_error(nvl);
		return;
	}

	nvp = nvpair_create_stringv(name, valuefmt, valueap);
	if (nvp == NULL)
		nvl->nvl_error = errno = (errno != 0 ? errno : ENOMEM);
	else
		nvlist_move_nvpair(nvl, nvp);
}

void
nvlist_add_nvlist(nvlist_t *nvl, const char *name, const nvlist_t *value)
{

	nvlist_addf_nvlist(nvl, value, "%s", name);
}

void
nvlist_add_binary(nvlist_t *nvl, const char *name, const void *value,
    size_t size)
{

	nvlist_addf_binary(nvl, value, size, "%s", name);
}

void
nvlist_addf_null(nvlist_t *nvl, const char *namefmt, ...)
{
	va_list nameap;

	va_start(nameap, namefmt);
	nvlist_addv_null(nvl, namefmt, nameap);
	va_end(nameap);
}

void
nvlist_addf_bool(nvlist_t *nvl, bool value, const char *namefmt, ...)
{
	va_list nameap;

	va_start(nameap, namefmt);
	nvlist_addv_bool(nvl, value, namefmt, nameap);
	va_end(nameap);
}

void
nvlist_addf_number(nvlist_t *nvl, uint64_t value, const char *namefmt, ...)
{
	va_list nameap;

	va_start(nameap, namefmt);
	nvlist_addv_number(nvl, value, namefmt, nameap);
	va_end(nameap);
}

void
nvlist_addf_string(nvlist_t *nvl, const char *value, const char *namefmt, ...)
{
	va_list nameap;

	va_start(nameap, namefmt);
	nvlist_addv_string(nvl, value, namefmt, nameap);
	va_end(nameap);
}

void
nvlist_addf_nvlist(nvlist_t *nvl, const nvlist_t *value, const char *namefmt,
    ...)
{
	va_list nameap;

	va_start(nameap, namefmt);
	nvlist_addv_nvlist(nvl, value, namefmt, nameap);
	va_end(nameap);
}

void
nvlist_addf_binary(nvlist_t *nvl, const void *value, size_t size,
    const char *namefmt, ...)
{
	va_list nameap;

	va_start(nameap, namefmt);
	nvlist_addv_binary(nvl, value, size, namefmt, nameap);
	va_end(nameap);
}

void
nvlist_addv_null(nvlist_t *nvl, const char *namefmt, va_list nameap)
{
	nvpair_t *nvp;

	if (nvlist_error(nvl) != 0) {
		errno = nvlist_error(nvl);
		return;
	}

	nvp = nvpair_createv_null(namefmt, nameap);
	if (nvp == NULL)
		nvl->nvl_error = errno = (errno != 0 ? errno : ENOMEM);
	else
		nvlist_move_nvpair(nvl, nvp);
}

void
nvlist_addv_bool(nvlist_t *nvl, bool value, const char *namefmt, va_list nameap)
{
	nvpair_t *nvp;

	if (nvlist_error(nvl) != 0) {
		errno = nvlist_error(nvl);
		return;
	}

	nvp = nvpair_createv_bool(value, namefmt, nameap);
	if (nvp == NULL)
		nvl->nvl_error = errno = (errno != 0 ? errno : ENOMEM);
	else
		nvlist_move_nvpair(nvl, nvp);
}

void
nvlist_addv_number(nvlist_t *nvl, uint64_t value, const char *namefmt,
    va_list nameap)
{
	nvpair_t *nvp;

	if (nvlist_error(nvl) != 0) {
		errno = nvlist_error(nvl);
		return;
	}

	nvp = nvpair_createv_number(value, namefmt, nameap);
	if (nvp == NULL)
		nvl->nvl_error = errno = (errno != 0 ? errno : ENOMEM);
	else
		nvlist_move_nvpair(nvl, nvp);
}

void
nvlist_addv_string(nvlist_t *nvl, const char *value, const char *namefmt,
    va_list nameap)
{
	nvpair_t *nvp;

	if (nvlist_error(nvl) != 0) {
		errno = nvlist_error(nvl);
		return;
	}

	nvp = nvpair_createv_string(value, namefmt, nameap);
	if (nvp == NULL)
		nvl->nvl_error = errno = (errno != 0 ? errno : ENOMEM);
	else
		nvlist_move_nvpair(nvl, nvp);
}

void
nvlist_addv_nvlist(nvlist_t *nvl, const nvlist_t *value, const char *namefmt,
    va_list nameap)
{
	nvpair_t *nvp;

	if (nvlist_error(nvl) != 0) {
		errno = nvlist_error(nvl);
		return;
	}

	nvp = nvpair_createv_nvlist(value, namefmt, nameap);
	if (nvp == NULL)
		nvl->nvl_error = errno = (errno != 0 ? errno : ENOMEM);
	else
		nvlist_move_nvpair(nvl, nvp);
}


void
nvlist_addv_binary(nvlist_t *nvl, const void *value, size_t size,
    const char *namefmt, va_list nameap)
{
	nvpair_t *nvp;

	if (nvlist_error(nvl) != 0) {
		errno = nvlist_error(nvl);
		return;
	}

	nvp = nvpair_createv_binary(value, size, namefmt, nameap);
	if (nvp == NULL)
		nvl->nvl_error = errno = (errno != 0 ? errno : ENOMEM);
	else
		nvlist_move_nvpair(nvl, nvp);
}

void
nvlist_move_nvpair(nvlist_t *nvl, nvpair_t *nvp)
{

	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvpair_nvlist(nvp) == NULL);

	if (nvlist_error(nvl) != 0) {
		nvpair_free(nvp);
		errno = nvlist_error(nvl);
		return;
	}
	if (nvlist_exists(nvl, nvpair_name(nvp))) {
		nvpair_free(nvp);
		nvl->nvl_error = errno = EEXIST;
		return;
	}

	nvpair_insert(&nvl->nvl_head, nvp, nvl);
}

NVLIST_MOVE(char *, string)
NVLIST_MOVE(nvlist_t *, nvlist)

void
nvlist_move_binary(nvlist_t *nvl, const char *name, void *value, size_t size)
{

	nvlist_movef_binary(nvl, value, size, "%s", name);
}

NVLIST_MOVEF(char *, string)
NVLIST_MOVEF(nvlist_t *, nvlist)

void
nvlist_movef_binary(nvlist_t *nvl, void *value, size_t size,
    const char *namefmt, ...)
{
	va_list nameap;

	va_start(nameap, namefmt);
	nvlist_movev_binary(nvl, value, size, namefmt, nameap);
	va_end(nameap);
}

void
nvlist_movev_string(nvlist_t *nvl, char *value, const char *namefmt,
    va_list nameap)
{
	nvpair_t *nvp;

	if (nvlist_error(nvl) != 0) {
		free(value);
		errno = nvlist_error(nvl);
		return;
	}

	nvp = nvpair_movev_string(value, namefmt, nameap);
	if (nvp == NULL)
		nvl->nvl_error = errno = (errno != 0 ? errno : ENOMEM);
	else
		nvlist_move_nvpair(nvl, nvp);
}

void
nvlist_movev_nvlist(nvlist_t *nvl, nvlist_t *value, const char *namefmt,
    va_list nameap)
{
	nvpair_t *nvp;

	if (nvlist_error(nvl) != 0) {
		if (value != NULL && nvlist_get_nvpair_parent(value) != NULL)
			nvlist_destroy(value);
		errno = nvlist_error(nvl);
		return;
	}

	nvp = nvpair_movev_nvlist(value, namefmt, nameap);
	if (nvp == NULL)
		nvl->nvl_error = errno = (errno != 0 ? errno : ENOMEM);
	else
		nvlist_move_nvpair(nvl, nvp);
}


void
nvlist_movev_binary(nvlist_t *nvl, void *value, size_t size,
    const char *namefmt, va_list nameap)
{
	nvpair_t *nvp;

	if (nvlist_error(nvl) != 0) {
		free(value);
		errno = nvlist_error(nvl);
		return;
	}

	nvp = nvpair_movev_binary(value, size, namefmt, nameap);
	if (nvp == NULL)
		nvl->nvl_error = errno = (errno != 0 ? errno : ENOMEM);
	else
		nvlist_move_nvpair(nvl, nvp);
}

const nvpair_t *
nvlist_get_nvpair(const nvlist_t *nvl, const char *name)
{

	return (nvlist_find(nvl, NV_TYPE_NONE, name));
}

NVLIST_GET(bool, bool, BOOL)
NVLIST_GET(uint64_t, number, NUMBER)
NVLIST_GET(const char *, string, STRING)
NVLIST_GET(const nvlist_t *, nvlist, NVLIST)

const void *
nvlist_get_binary(const nvlist_t *nvl, const char *name, size_t *sizep)
{
	nvpair_t *nvp;

	nvp = nvlist_find(nvl, NV_TYPE_BINARY, name);
	if (nvp == NULL)
		nvlist_report_missing(NV_TYPE_BINARY, name);

	return (nvpair_get_binary(nvp, sizep));
}

NVLIST_GETF(bool, bool)
NVLIST_GETF(uint64_t, number)
NVLIST_GETF(const char *, string)
NVLIST_GETF(const nvlist_t *, nvlist)

const void *
nvlist_getf_binary(const nvlist_t *nvl, size_t *sizep, const char *namefmt, ...)
{
	va_list nameap;
	const void *value;

	va_start(nameap, namefmt);
	value = nvlist_getv_binary(nvl, sizep, namefmt, nameap);
	va_end(nameap);

	return (value);
}

NVLIST_GETV(bool, bool, BOOL)
NVLIST_GETV(uint64_t, number, NUMBER)
NVLIST_GETV(const char *, string, STRING)
NVLIST_GETV(const nvlist_t *, nvlist, NVLIST)

const void *
nvlist_getv_binary(const nvlist_t *nvl, size_t *sizep, const char *namefmt,
    va_list nameap)
{
	char *name;
	const void *binary;

	vasprintf(&name, namefmt, nameap);
	if (name == NULL)
		nvlist_report_missing(NV_TYPE_BINARY, "<unknown>");

	binary = nvlist_get_binary(nvl, name, sizep);
	free(name);
	return (binary);
}

nvpair_t *
nvlist_take_nvpair(nvlist_t *nvl, const char *name)
{
	nvpair_t *nvp;

	nvp = nvlist_find(nvl, NV_TYPE_NONE, name);
	if (nvp != NULL)
		nvlist_remove_nvpair(nvl, nvp);
	return (nvp);
}

NVLIST_TAKE(bool, bool, BOOL)
NVLIST_TAKE(uint64_t, number, NUMBER)
NVLIST_TAKE(char *, string, STRING)

nvlist_t *
nvlist_take_nvlist(nvlist_t *nvl, const char *name)
{
	nvpair_t *nvp;
	nvlist_t *value;

	nvp = nvlist_find(nvl, NV_TYPE_NVLIST, name);
	if (nvp == NULL)
		nvlist_report_missing(NV_TYPE_NVLIST, name);
	value = (nvlist_t *)(intptr_t)nvpair_get_nvlist(nvp);
	nvlist_remove_nvpair(nvl, nvp);
	nvpair_free_structure(nvp);
	return (value);
}

void *
nvlist_take_binary(nvlist_t *nvl, const char *name, size_t *sizep)
{
	nvpair_t *nvp;
	void *value;

	nvp = nvlist_find(nvl, NV_TYPE_BINARY, name);
	if (nvp == NULL)
		nvlist_report_missing(NV_TYPE_BINARY, name);

	value = (void *)(intptr_t)nvpair_get_binary(nvp, sizep);
	nvlist_remove_nvpair(nvl, nvp);
	nvpair_free_structure(nvp);
	return (value);
}

NVLIST_TAKEF(bool, bool)
NVLIST_TAKEF(uint64_t, number)
NVLIST_TAKEF(char *, string)
NVLIST_TAKEF(nvlist_t *, nvlist)

void *
nvlist_takef_binary(nvlist_t *nvl, size_t *sizep, const char *namefmt, ...)
{
	va_list nameap;
	void *value;

	va_start(nameap, namefmt);
	value = nvlist_takev_binary(nvl, sizep, namefmt, nameap);
	va_end(nameap);

	return (value);
}

NVLIST_TAKEV(bool, bool, BOOL)
NVLIST_TAKEV(uint64_t, number, NUMBER)
NVLIST_TAKEV(char *, string, STRING)
NVLIST_TAKEV(nvlist_t *, nvlist, NVLIST)

void *
nvlist_takev_binary(nvlist_t *nvl, size_t *sizep, const char *namefmt,
    va_list nameap)
{
	char *name;
	void *binary;

	vasprintf(&name, namefmt, nameap);
	if (name == NULL)
		nvlist_report_missing(NV_TYPE_BINARY, "<unknown>");

	binary = nvlist_take_binary(nvl, name, sizep);
	free(name);
	return (binary);
}

void
nvlist_remove_nvpair(nvlist_t *nvl, nvpair_t *nvp)
{

	NVLIST_ASSERT(nvl);
	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvpair_nvlist(nvp) == nvl);

	nvpair_remove(&nvl->nvl_head, nvp, nvl);
}

void
nvlist_free(nvlist_t *nvl, const char *name)
{

	nvlist_free_type(nvl, name, NV_TYPE_NONE);
}

NVLIST_FREE(null, NULL)
NVLIST_FREE(bool, BOOL)
NVLIST_FREE(number, NUMBER)
NVLIST_FREE(string, STRING)
NVLIST_FREE(nvlist, NVLIST)
NVLIST_FREE(binary, BINARY)

void
nvlist_freef(nvlist_t *nvl, const char *namefmt, ...)
{
	va_list nameap;

	va_start(nameap, namefmt);
	nvlist_freev(nvl, namefmt, nameap);
	va_end(nameap);
}

NVLIST_FREEF(null)
NVLIST_FREEF(bool)
NVLIST_FREEF(number)
NVLIST_FREEF(string)
NVLIST_FREEF(nvlist)
NVLIST_FREEF(binary)

void
nvlist_freev(nvlist_t *nvl, const char *namefmt, va_list nameap)
{
	char *name;

	vasprintf(&name, namefmt, nameap);
	if (name == NULL)
		nvlist_report_missing(NV_TYPE_NONE, "<unknown>");
	nvlist_free(nvl, name);
	free(name);
}

NVLIST_FREEV(null, NULL)
NVLIST_FREEV(bool, BOOL)
NVLIST_FREEV(number, NUMBER)
NVLIST_FREEV(string, STRING)
NVLIST_FREEV(nvlist, NVLIST)
NVLIST_FREEV(binary, BINARY)

void
nvlist_free_nvpair(nvlist_t *nvl, nvpair_t *nvp)
{

	NVLIST_ASSERT(nvl);
	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvpair_nvlist(nvp) == nvl);

	nvlist_remove_nvpair(nvl, nvp);
	nvpair_free(nvp);
}

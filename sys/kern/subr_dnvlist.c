/*-
 * Copyright (c) 2013 The FreeBSD Foundation
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

#ifdef _KERNEL

#include <sys/types.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/malloc.h>

#include <machine/stdarg.h>

#else
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#endif

#include <sys/nv.h>
#include <sys/nv_impl.h>
#include <sys/nvlist_getters.h>

#include <sys/dnv.h>

DNVLIST_GET(bool, bool)
DNVLIST_GET(uint64_t, number)
DNVLIST_GET(const char *, string)
DNVLIST_GET(const nvlist_t *, nvlist)

const void *
dnvlist_get_binary(const nvlist_t *nvl, const char *name, size_t *sizep,
    const void *defval, size_t defsize)
{
	const void *value;

	if (nvlist_exists_binary(nvl, name))
		value = nvlist_get_binary(nvl, name, sizep);
	else {
		if (sizep != NULL)
			*sizep = defsize;
		value = defval;
	}
	return (value);
}

#ifndef _KERNEL
DNVLIST_GETF(bool, bool)
DNVLIST_GETF(uint64_t, number)
DNVLIST_GETF(const char *, string)
DNVLIST_GETF(const nvlist_t *, nvlist)

const void *
dnvlist_getf_binary(const nvlist_t *nvl, size_t *sizep, const void *defval,
    size_t defsize, const char *namefmt, ...)
{
	va_list nameap;
	const void *value;

	va_start(nameap, namefmt);
	value = dnvlist_getv_binary(nvl, sizep, defval, defsize, namefmt,
	    nameap);
	va_end(nameap);

	return (value);
}

DNVLIST_GETV(bool, bool)
DNVLIST_GETV(uint64_t, number)
DNVLIST_GETV(const char *, string)
DNVLIST_GETV(const nvlist_t *, nvlist)

const void *
dnvlist_getv_binary(const nvlist_t *nvl, size_t *sizep, const void *defval,
    size_t defsize, const char *namefmt, va_list nameap)
{
	char *name;
	const void *value;

	nv_vasprintf(&name, namefmt, nameap);
	if (name != NULL) {
		value = dnvlist_get_binary(nvl, name, sizep, defval, defsize);
		nv_free(name);
	} else {
		if (sizep != NULL)
			*sizep = defsize;
		value = defval;
	}
	return (value);
}
#endif

DNVLIST_TAKE(bool, bool)
DNVLIST_TAKE(uint64_t, number)
DNVLIST_TAKE(char *, string)
DNVLIST_TAKE(nvlist_t *, nvlist)

void *
dnvlist_take_binary(nvlist_t *nvl, const char *name, size_t *sizep,
    void *defval, size_t defsize)
{
	void *value;

	if (nvlist_exists_binary(nvl, name))
		value = nvlist_take_binary(nvl, name, sizep);
	else {
		if (sizep != NULL)
			*sizep = defsize;
		value = defval;
	}
	return (value);
}

#ifndef _KERNEL
DNVLIST_TAKEF(bool, bool)
DNVLIST_TAKEF(uint64_t, number)
DNVLIST_TAKEF(char *, string)
DNVLIST_TAKEF(nvlist_t *, nvlist)

void *
dnvlist_takef_binary(nvlist_t *nvl, size_t *sizep, void *defval,
    size_t defsize, const char *namefmt, ...)
{
	va_list nameap;
	void *value;

	va_start(nameap, namefmt);
	value = dnvlist_takev_binary(nvl, sizep, defval, defsize, namefmt,
	    nameap);
	va_end(nameap);

	return (value);
}

DNVLIST_TAKEV(bool, bool)
DNVLIST_TAKEV(uint64_t, number)
DNVLIST_TAKEV(char *, string)
DNVLIST_TAKEV(nvlist_t *, nvlist)

void *
dnvlist_takev_binary(nvlist_t *nvl, size_t *sizep, void *defval,
    size_t defsize, const char *namefmt, va_list nameap)
{
	char *name;
	void *value;

	nv_vasprintf(&name, namefmt, nameap);
	if (name != NULL) {
		value = dnvlist_take_binary(nvl, name, sizep, defval, defsize);
		nv_free(name);
	} else {
		if (sizep != NULL)
			*sizep = defsize;
		value = defval;
	}

	return (value);
}
#endif

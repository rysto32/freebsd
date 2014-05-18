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
 *
 * $FreeBSD$
 */

#ifndef _NVLIST_GETTERS_H
#define _NVLIST_GETTERS_H

nvpair_t *	nvlist_find(const nvlist_t *nvl, int type, const char *name);
void		nvlist_report_missing(int type, const char *name);

#define	DNVLIST_GET(ftype, type)					\
ftype									\
dnvlist_get_##type(const nvlist_t *nvl, const char *name, ftype defval)	\
{									\
									\
	if (nvlist_exists_##type(nvl, name))				\
		return (nvlist_get_##type(nvl, name));			\
	else								\
		return (defval);					\
}

#define	DNVLIST_GETF(ftype, type)					\
ftype									\
dnvlist_getf_##type(const nvlist_t *nvl, ftype defval,			\
    const char *namefmt, ...)						\
{									\
	va_list nameap;							\
	ftype value;							\
									\
	va_start(nameap, namefmt);					\
	value = dnvlist_getv_##type(nvl, defval, namefmt, nameap);	\
	va_end(nameap);							\
									\
	return (value);							\
}

#define	DNVLIST_GETV(ftype, type)					\
ftype									\
dnvlist_getv_##type(const nvlist_t *nvl, ftype defval,			\
    const char *namefmt, va_list nameap)				\
{									\
	char *name;							\
	ftype value;							\
									\
	vasprintf(&name, namefmt, nameap);				\
	if (name == NULL)						\
		return (defval);					\
	value = dnvlist_get_##type(nvl, name, defval);			\
	free(name);							\
	return (value);							\
}

#define	DNVLIST_TAKE(ftype, type)					\
ftype									\
dnvlist_take_##type(nvlist_t *nvl, const char *name, ftype defval)	\
{									\
									\
	if (nvlist_exists_##type(nvl, name))				\
		return (nvlist_take_##type(nvl, name));			\
	else								\
		return (defval);					\
}

#define	DNVLIST_TAKEF(ftype, type)					\
ftype									\
dnvlist_takef_##type(nvlist_t *nvl, ftype defval,			\
    const char *namefmt, ...)						\
{									\
	va_list nameap;							\
	ftype value;							\
									\
	va_start(nameap, namefmt);					\
	value = dnvlist_takev_##type(nvl, defval, namefmt, nameap);	\
	va_end(nameap);							\
									\
	return (value);							\
}

#define	DNVLIST_TAKEV(ftype, type)					\
ftype									\
dnvlist_takev_##type(nvlist_t *nvl, ftype defval, const char *namefmt,	\
    va_list nameap)							\
{									\
	char *name;							\
	ftype value;							\
									\
	vasprintf(&name, namefmt, nameap);				\
	if (name == NULL)						\
		return (defval);					\
	value = dnvlist_take_##type(nvl, name, defval);			\
	free(name);							\
	return (value);							\
}

#define	NVLIST_EXISTS(type, TYPE)					\
bool									\
nvlist_exists_##type(const nvlist_t *nvl, const char *name)		\
{									\
									\
	return (nvlist_find(nvl, NV_TYPE_##TYPE, name) != NULL);	\
}

#define	NVLIST_EXISTSF(type)						\
bool									\
nvlist_existsf_##type(const nvlist_t *nvl, const char *namefmt, ...)	\
{									\
	va_list nameap;							\
	bool ret;							\
									\
	va_start(nameap, namefmt);					\
	ret = nvlist_existsv_##type(nvl, namefmt, nameap);		\
	va_end(nameap);							\
	return (ret);							\
}

#define	NVLIST_EXISTSV(type)						\
bool									\
nvlist_existsv_##type(const nvlist_t *nvl, const char *namefmt,		\
    va_list nameap)							\
{									\
	char *name;							\
	bool exists;							\
									\
	vasprintf(&name, namefmt, nameap);				\
	if (name == NULL)						\
		return (0);						\
	exists = nvlist_exists_##type(nvl, name);			\
	free(name);							\
	return (exists);						\
}

#define	NVLIST_MOVE(vtype, type)					\
void									\
nvlist_move_##type(nvlist_t *nvl, const char *name, vtype value)	\
{									\
									\
	nvlist_movef_##type(nvl, value, "%s", name);			\
}

#define	NVLIST_MOVEF(vtype, type)					\
void									\
nvlist_movef_##type(nvlist_t *nvl, vtype value, const char *namefmt,	\
    ...)								\
{									\
	va_list nameap;							\
									\
	va_start(nameap, namefmt);					\
	nvlist_movev_##type(nvl, value, namefmt, nameap);		\
	va_end(nameap);							\
}

#define	NVLIST_GET(ftype, type, TYPE)					\
ftype									\
nvlist_get_##type(const nvlist_t *nvl, const char *name)		\
{									\
	const nvpair_t *nvp;						\
									\
	nvp = nvlist_find(nvl, NV_TYPE_##TYPE, name);			\
	if (nvp == NULL)						\
		nvlist_report_missing(NV_TYPE_##TYPE, name);		\
	return (nvpair_get_##type(nvp));				\
}

#define	NVLIST_GETF(ftype, type)					\
ftype									\
nvlist_getf_##type(const nvlist_t *nvl, const char *namefmt, ...)	\
{									\
	va_list nameap;							\
	ftype value;							\
									\
	va_start(nameap, namefmt);					\
	value = nvlist_getv_##type(nvl, namefmt, nameap);		\
	va_end(nameap);							\
									\
	return (value);							\
}

#define	NVLIST_GETV(ftype, type, TYPE)					\
ftype									\
nvlist_getv_##type(const nvlist_t *nvl, const char *namefmt,		\
    va_list nameap)							\
{									\
	char *name;							\
	ftype value;							\
									\
	vasprintf(&name, namefmt, nameap);				\
	if (name == NULL)						\
		nvlist_report_missing(NV_TYPE_##TYPE, "<unknown>");	\
	value = nvlist_get_##type(nvl, name);				\
	free(name);							\
									\
	return (value);							\
}

#define	NVLIST_TAKE(ftype, type, TYPE)					\
ftype									\
nvlist_take_##type(nvlist_t *nvl, const char *name)			\
{									\
	nvpair_t *nvp;							\
	ftype value;							\
									\
	nvp = nvlist_find(nvl, NV_TYPE_##TYPE, name);			\
	if (nvp == NULL)						\
		nvlist_report_missing(NV_TYPE_##TYPE, name);		\
	value = (ftype)(intptr_t)nvpair_get_##type(nvp);		\
	nvlist_remove_nvpair(nvl, nvp);					\
	nvpair_free_structure(nvp);					\
	return (value);							\
}

#define	NVLIST_TAKEF(ftype, type)					\
ftype									\
nvlist_takef_##type(nvlist_t *nvl, const char *namefmt, ...)		\
{									\
	va_list nameap;							\
	ftype value;							\
									\
	va_start(nameap, namefmt);					\
	value = nvlist_takev_##type(nvl, namefmt, nameap);		\
	va_end(nameap);							\
									\
	return (value);							\
}

#define	NVLIST_TAKEV(ftype, type, TYPE)					\
ftype									\
nvlist_takev_##type(nvlist_t *nvl, const char *namefmt, va_list nameap)	\
{									\
	char *name;							\
	ftype value;							\
									\
	vasprintf(&name, namefmt, nameap);				\
	if (name == NULL)						\
		nvlist_report_missing(NV_TYPE_##TYPE, "<unknown>");	\
	value = nvlist_take_##type(nvl, name);				\
	free(name);							\
	return (value);							\
}

#define	NVLIST_FREE(type, TYPE)						\
void									\
nvlist_free_##type(nvlist_t *nvl, const char *name)			\
{									\
									\
	nvlist_free_type(nvl, name, NV_TYPE_##TYPE);			\
}

#define	NVLIST_FREEF(type)						\
void									\
nvlist_freef_##type(nvlist_t *nvl, const char *namefmt, ...)		\
{									\
	va_list nameap;							\
									\
	va_start(nameap, namefmt);					\
	nvlist_freev_##type(nvl, namefmt, nameap);			\
	va_end(nameap);							\
}

#define	NVLIST_FREEV(type, TYPE)					\
void									\
nvlist_freev_##type(nvlist_t *nvl, const char *namefmt, va_list nameap)	\
{									\
	char *name;							\
									\
	vasprintf(&name, namefmt, nameap);				\
	if (name == NULL)						\
		nvlist_report_missing(NV_TYPE_##TYPE, "<unknown>");	\
	nvlist_free_##type(nvl, name);					\
	free(name);							\
}

#endif

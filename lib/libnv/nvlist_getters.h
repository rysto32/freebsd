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

nvpair_t *	nvlist_findv(const nvlist_t *nvl, int type, const char *namefmt,
		    va_list nameap);
void	nvlist_report_missing(int type, const char *namefmt, va_list nameap);

#define	DNVLIST_GET(ftype, type)					\
ftype									\
dnvlist_get_##type(const nvlist_t *nvl, const char *name, ftype defval)	\
{									\
									\
	return (dnvlist_getf_##type(nvl, defval, "%s", name));		\
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
	va_list cnameap;						\
	ftype value;							\
									\
	va_copy(cnameap, nameap);					\
	if (nvlist_existsv_##type(nvl, namefmt, cnameap))		\
		value = nvlist_getv_##type(nvl, namefmt, nameap);	\
	else								\
		value = defval;						\
	va_end(cnameap);						\
	return (value);							\
}

#define	DNVLIST_TAKE(ftype, type)					\
ftype									\
dnvlist_take_##type(nvlist_t *nvl, const char *name, ftype defval)	\
{									\
									\
	return (dnvlist_takef_##type(nvl, defval, "%s", name));		\
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
	va_list cnameap;						\
	ftype value;							\
									\
	va_copy(cnameap, nameap);					\
	if (nvlist_existsv_##type(nvl, namefmt, cnameap))		\
		value = nvlist_takev_##type(nvl, namefmt, nameap);	\
	else								\
		value = defval;						\
	va_end(cnameap);						\
	return (value);							\
}

#define	NVLIST_EXISTS(type)						\
bool									\
nvlist_exists_##type(const nvlist_t *nvl, const char *name)		\
{									\
									\
	return (nvlist_existsf_##type(nvl, "%s", name));		\
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

#define	NVLIST_EXISTSV(type, TYPE)					\
bool									\
nvlist_existsv_##type(const nvlist_t *nvl, const char *namefmt,		\
    va_list nameap)							\
{									\
									\
	return (nvlist_findv(nvl, NV_TYPE_##TYPE, namefmt, nameap) !=	\
	    NULL);							\
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

#define	NVLIST_GET(ftype, type)						\
ftype									\
nvlist_get_##type(const nvlist_t *nvl, const char *name)		\
{									\
									\
	return (nvlist_getf_##type(nvl, "%s", name));			\
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
	va_list cnameap;						\
	const nvpair_t *nvp;						\
									\
	va_copy(cnameap, nameap);					\
	nvp = nvlist_findv(nvl, NV_TYPE_##TYPE, namefmt, cnameap);	\
	va_end(cnameap);						\
	if (nvp == NULL)						\
		nvlist_report_missing(NV_TYPE_##TYPE, namefmt, nameap);	\
	return (nvpair_get_##type(nvp));				\
}

#define	NVLIST_TAKE(ftype, type)					\
ftype									\
nvlist_take_##type(nvlist_t *nvl, const char *name)			\
{									\
									\
	return (nvlist_takef_##type(nvl, "%s", name));			\
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
	va_list cnameap;						\
	nvpair_t *nvp;							\
	ftype value;							\
									\
	va_copy(cnameap, nameap);					\
	nvp = nvlist_findv(nvl, NV_TYPE_##TYPE, namefmt, cnameap);	\
	va_end(cnameap);						\
	if (nvp == NULL)						\
		nvlist_report_missing(NV_TYPE_##TYPE, namefmt, nameap);	\
	value = (ftype)(intptr_t)nvpair_get_##type(nvp);		\
	nvlist_remove_nvpair(nvl, nvp);					\
	nvpair_free_structure(nvp);					\
	return (value);							\
}

#define	NVLIST_FREE(type)						\
void									\
nvlist_free_##type(nvlist_t *nvl, const char *name)			\
{									\
									\
	nvlist_freef_##type(nvl, "%s", name);				\
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
									\
	nvlist_freev_type(nvl, NV_TYPE_##TYPE, namefmt, nameap);	\
}

#endif

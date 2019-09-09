
/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2019 Ryan Stone
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _SYS_EBPF_PROBE_H_
#define _SYS_EBPF_PROBE_H_

#include <ck_queue.h>

#ifdef EBPF_HOOKS

struct ebpf_probe
{
	const char * name;
	int active;
	void *module_state;
	CK_SLIST_ENTRY(ebpf_probe) hash_link;
};

typedef void ebpf_fire_t(struct ebpf_probe *, uintptr_t arg0, uintptr_t arg1,
    uintptr_t arg2, uintptr_t arg3, uintptr_t arg4, uintptr_t arg5);

struct ebpf_module
{
	ebpf_fire_t * fire;
};

void ebpf_probe_register(void *);
void ebpf_probe_deregister(void *);

struct ebpf_probe * ebpf_find_probe(const char *);
void ebpf_probe_drain(struct ebpf_probe *);

void ebpf_module_register(struct ebpf_module *);
void ebpf_module_deregister(void);

void ebpf_probe_fire(struct ebpf_probe *, uintptr_t arg0, uintptr_t arg1,
    uintptr_t arg2, uintptr_t arg3, uintptr_t arg4, uintptr_t arg5);

#define _EBPF_PROBE(name) __CONCAT(name, _probe_def)

#define EBPF_PROBE_DEFINE(probeName) \
	static struct ebpf_probe _EBPF_PROBE(probeName) = { \
		.name = __XSTRING(probeName), \
		.active = 0, \
	}; \
	SYSINIT(__CONCAT(epf_, __CONCAT(probeName, _register)), SI_SUB_DTRACE, SI_ORDER_SECOND, \
		ebpf_probe_register, &_EBPF_PROBE(probeName)); \
	SYSUNINIT(__CONCAT(epf_, __CONCAT(probeName, _deregister)), SI_SUB_DTRACE, SI_ORDER_SECOND, \
		ebpf_probe_deregister, &_EBPF_PROBE(probeName)); \
	struct hack

#define EBPF_PROBE_ACTIVE(name) \
	(_EBPF_PROBE(name).active)

#define EBPF_PROBE_FIRE6(name, arg0, arg1, arg2, arg3, arg4, arg5) \
	do { \
		if (_EBPF_PROBE(name).active) { \
			ebpf_probe_fire(&_EBPF_PROBE(name), \
			    (uintptr_t)arg0, (uintptr_t)arg1, (uintptr_t)arg2, \
			    (uintptr_t)arg3, (uintptr_t)arg4, (uintptr_t)arg5); \
		} \
	} while (0)

#else

#define EBPF_PROBE_DEFINE(probeName) struct hack

#define EBPF_PROBE_FIRE6(name, arg0, arg1, arg2, arg3, arg4, arg5) do { \
		(void)arg0; \
		(void)arg1; \
		(void)arg2; \
		(void)arg3; \
		(void)arg4; \
		(void)arg5; \
	} while (0)

#endif

#define EBPF_PROBE_FIRE1(name, arg0) \
	EBPF_PROBE_FIRE6(name, arg0, 0, 0, 0, 0, 0)

#define EBPF_PROBE_FIRE2(name, arg0, arg1) \
	EBPF_PROBE_FIRE6(name, arg0, arg1, 0, 0, 0, 0)

#define EBPF_PROBE_FIRE4(name, arg0, arg1, arg2, arg3) \
	EBPF_PROBE_FIRE6(name, arg0, arg1, arg2, arg3, 0, 0)

#define	EBPF_ACTION_CONTINUE	0
#define	EBPF_ACTION_DUP		1
#define	EBPF_ACTION_OPENAT	2
#define	EBPF_ACTION_FSTATAT	3
#define	EBPF_ACTION_FSTAT	4

struct open_probe_args
{
	int *fd;
	char * path;
	int mode;
	int *action;
};

struct stat_probe_args
{
	int *fd;
	char * path;
	int *action;
};

#endif

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

#ifndef	_NVLIST_IMPL_H_
#define	_NVLIST_IMPL_H_

#ifndef _KERNEL
#include <stdint.h>
#endif

#include "nv.h"

TAILQ_HEAD(nvl_head, nvpair);

#define	NVLIST_MAGIC	0x6e766c	/* "nvl" */
struct nvlist {
	int		nvl_magic;
	int		nvl_error;
	int		nvl_flags;
	int		nvl_depth;
	struct nvl_head	nvl_head;
};

#define	NVLIST_HEADER_MAGIC	0x6c
#define	NVLIST_HEADER_VERSION	0x00
struct nvlist_header {
	uint8_t		nvlh_magic;
	uint8_t		nvlh_version;
	uint8_t		nvlh_flags;
	uint64_t	nvlh_descriptors;
	uint64_t	nvlh_size;
} __packed;

void *nvlist_xpack(const nvlist_t *nvl, int64_t *fdidxp, size_t *sizep);
nvlist_t *nvlist_xunpack(const void *buf, size_t size, const int *fds,
    size_t nfds, int level);
bool nvlist_check_header(struct nvlist_header *nvlhdrp);

nvpair_t *nvlist_get_nvpair_parent(const nvlist_t *nvl);
const unsigned char *nvlist_unpack_header(nvlist_t *nvl,
    const unsigned char *ptr, size_t nfds, bool *isbep, size_t *leftp);

#endif	/* !_NVLIST_IMPL_H_ */

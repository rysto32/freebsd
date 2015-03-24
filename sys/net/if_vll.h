/*-
 * Copyright (c) 2012, Sandvine Incorporated
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the developer may NOT be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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

#ifndef _NET_IF_VLL_H_
#define _NET_IF_VLL_H_

typedef void vll_init_t(void *);
typedef int vll_ioctl_t(void *, u_long, caddr_t);
typedef int vll_transmit_t(void *, struct mbuf *);
typedef void vll_qflush_t(void *);

struct vll_softc
{
	struct rmlock vll_lock;
	struct ifnet *vll_ifp;
	struct ifnet *vll_parent;

	void *vll_softc;
	vll_init_t *vll_init;
	vll_ioctl_t *vll_ioctl;
	vll_transmit_t *vll_transmit;
	vll_qflush_t *vll_qflush;

	uint32_t vll_flags;
};

typedef int vll_attach_t(struct ifnet *parent, struct vll_softc *vll);
typedef int vll_detach_t(struct vll_softc *);

struct vll_methods
{
	vll_attach_t *vll_attach;
	vll_detach_t *vll_detach;
};


#endif


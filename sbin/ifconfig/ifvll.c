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

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/sockio.h>

#include <stdlib.h>
#include <unistd.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/vll.h>
#include <net/route.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>
#include <errno.h>

#include "ifconfig.h"

static struct vllreq params;

static int
vll_exists(int s)
{
	struct vllreq vreq;

 	bzero(&vreq, sizeof(vreq));
	ifr.ifr_data = (caddr_t)&vreq;
	return (ioctl(s, SIOCGVLLPARENT, (caddr_t)&ifr) == 0);
}

static void
vll_set_parent(int s, const char *parent)
{
	strlcpy(params.parent, parent, sizeof(params.parent));

	if (vll_exists(s)) {
		ifr.ifr_data = (caddr_t)&params;
		if (ioctl(s, SIOCSVLLPARENT, (caddr_t)&ifr) == -1)
			err(1, "SIOCSVLLPARENT");
	}
}

static
DECL_CMD_FUNC(set_vll_parent, val, d)
{

	vll_set_parent(s, val);
}

static
DECL_CMD_FUNC(unset_vll_parent, val, d)
{

	vll_set_parent(s, "");
}

static void
vll_status(int s)
{
	struct vllreq vreq;

 	bzero(&vreq, sizeof(vreq));
	ifr.ifr_data = (caddr_t)&vreq;
	if (ioctl(s, SIOCGVLLPARENT, (caddr_t)&ifr) == 0)
		printf("\tvll parent interface: %s\n",
		    vreq.parent[0] == '\0' ? "<none>" : vreq.parent);
}

static void
vll_create(int s, struct ifreq *ifr)
{
	if (params.parent[0] != '\0')
		ifr->ifr_data = (caddr_t) &params;

	if (ioctl(s, SIOCIFCREATE2, ifr) < 0)
		err(1, "SIOCIFCREATE2");
}

static struct cmd vll_cmds[] = {
	DEF_CLONE_CMD_ARG("vlldev",			 set_vll_parent),
	/* NB: non-clone cmds */
	DEF_CMD_ARG("vlldev", 				 set_vll_parent),
	DEF_CMD_OPTARG("-vlldev",			 unset_vll_parent),
};

static struct afswtch af_vll = {
	.af_name	 = "af_vll",
	.af_af 	 = AF_UNSPEC,
	.af_other_status = vll_status,
};
 
static __constructor void
vll_ctor(void)
{
#define	N(a)	(sizeof(a) / sizeof(a[0]))
	size_t i;
 
	for (i = 0; i < N(vll_cmds);	i++)
		cmd_register(&vll_cmds[i]);
	af_register(&af_vll);
	clone_setdefcallback("vll", vll_create);
#undef N
}



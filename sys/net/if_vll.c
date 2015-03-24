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

#include "opt_virtual_if.h"

#include <sys/param.h>
#include <sys/types.h>
#include <sys/lock.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/rmlock.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_clone.h>
#include <net/if_types.h>
#include <net/if_vll.h>
#include <net/vll.h>
#include <net/vnet.h>

#ifndef VLL_HOOKS
#error "vll driver depends on VLL_HOOKS"
#endif

#define	VLL_NAME	"vll"

static MALLOC_DEFINE(M_VLL, "vll", "virtual link-layer interface allocations");

#define VLL_LOCK_INIT(_sc) rm_init(&(_sc)->vll_lock, "vll")
#define VLL_LOCK_DESTROY(_sc) rm_destroy(&(_sc)->vll_lock)
#define VLL_RLOCK(_sc, _tracker) rm_rlock(&(_sc)->vll_lock, (_tracker))
#define VLL_RUNLOCK(_sc, _tracker) rm_runlock(&(_sc)->vll_lock, (_tracker))
#define VLL_WLOCK(_sc) rm_wlock(&(_sc)->vll_lock)
#define VLL_WUNLOCK(_sc) rm_wunlock(&(_sc)->vll_lock)

#define VLL_DETACHED(_sc) ((_sc)->vll_parent == NULL)


#define VLL_ATTACHING 0x01
#define VLL_INFLUX(_sc) ((_sc)->vll_flags & VLL_ATTACHING)

#define VLL_CONFIGURED(_sc) (!VLL_DETACHED(_sc) && !VLL_INFLUX(_sc))

static int vll_attach(struct vll_softc *sc, struct ifnet *parent_ifp);
static int vll_detach(struct vll_softc *sc);


static int vll_clone_create(struct if_clone *, char *, size_t, caddr_t);
static int vll_clone_destroy(struct if_clone *, struct ifnet *);

static struct if_clone *vll_cloner;

#ifdef VIMAGE
static VNET_DEFINE(struct if_clone, vll_cloner);
#define	V_vll_cloner	VNET(vll_cloner)
#endif

FEATURE(vll, "Virtual link-layer ethernet driver framework");

static int
vll_modevent(module_t mod, int type, void *data)
{

	switch (type) {
	case MOD_LOAD:
#ifndef VIMAGE
		vll_cloner = if_clone_advanced(VLL_NAME, 0, ifc_simple_match,
		    vll_clone_create, vll_clone_destroy);
#endif
		if (bootverbose)
			printf("vll: initialized\n");
		break;
	case MOD_UNLOAD:
#ifndef VIMAGE
		if_clone_detach(vll_cloner);
#endif
		if (bootverbose)
			printf("vll: unloaded\n");
		break;
	default:
		return (EOPNOTSUPP);
	}
	return (0);
}

static moduledata_t vll_mod = {
	"if_vll",
	vll_modevent,
	0
};

DECLARE_MODULE(if_vll, vll_mod, SI_SUB_PSEUDO, SI_ORDER_ANY);
MODULE_VERSION(if_vll, 3);

#ifdef VIMAGE
static void
vnet_vll_init(const void *unused __unused)
{

	V_vll_cloner = vll_cloner;
	if_clone_attach(&V_vll_cloner);
}
VNET_SYSINIT(vnet_vll_init, SI_SUB_PROTO_IFATTACHDOMAIN, SI_ORDER_ANY,
    vnet_vll_init, NULL);

static void
vnet_vll_uninit(const void *unused __unused)
{

	if_clone_detach(&V_vll_cloner);
}
VNET_SYSUNINIT(vnet_vll_uninit, SI_SUB_PROTO_IFATTACHDOMAIN, SI_ORDER_FIRST,
    vnet_vll_uninit, NULL);
#endif

static int
vll_attach(struct vll_softc *sc, struct ifnet *parent_ifp)
{
	int error;

	error = vll_detach(sc);
	if (error != 0)
		return (error);

	if (parent_ifp->if_vll_funcs == NULL)
		return (ENODEV);

	VLL_WLOCK(sc);
	sc->vll_flags |= VLL_ATTACHING;
	VLL_WUNLOCK(sc);

	error = parent_ifp->if_vll_funcs->vll_attach(parent_ifp, sc);

	VLL_WLOCK(sc);
	sc->vll_flags &= ~VLL_ATTACHING;

	if (error == 0)
		sc->vll_parent = parent_ifp;
	VLL_WUNLOCK(sc);

	return (error);
}

static int
vll_detach(struct vll_softc *sc)
{
	int error;

	VLL_WLOCK(sc);

	/* If a conflicting attach/detach event is under way, bail out. */
	if (VLL_INFLUX(sc)) {
		VLL_WUNLOCK(sc);
		return (EBUSY);
	}
	
	/* There is nothing to do if we never attached to a parent. */
	if (VLL_DETACHED(sc)) {
		VLL_WUNLOCK(sc);
		return (0);
	}

	sc->vll_flags |= VLL_ATTACHING;
	VLL_WUNLOCK(sc);

	error = sc->vll_parent->if_vll_funcs->vll_detach(sc);

	VLL_WLOCK(sc);
	sc->vll_flags &= ~VLL_ATTACHING;

	if (error == 0)
		sc->vll_parent = NULL;
	VLL_WUNLOCK(sc);

	return (error);
}

static void
vll_init(void *arg)
{
	struct rm_priotracker tracker;
	struct vll_softc *sc;

	sc = arg;
	
	VLL_RLOCK(sc, &tracker);
	if (VLL_CONFIGURED(sc))
		sc->vll_init(sc->vll_softc);
	VLL_RUNLOCK(sc, &tracker);
}

static int
vll_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct rm_priotracker tracker;
	struct vllreq req;
	struct ifreq *ifr;
	struct vll_softc *sc;
	struct ifnet *parent_ifp;
	int error;

	ifr = (struct ifreq*)data;
	sc = ifp->if_softc;
	error = 0;

	switch (cmd) {
	case SIOCGVLLPARENT:
		bzero(&req, sizeof(req));

		VLL_RLOCK(sc, &tracker);
		if (VLL_CONFIGURED(sc))
			strlcpy(req.parent, sc->vll_parent->if_xname, IFNAMSIZ);
		VLL_RUNLOCK(sc, &tracker);

		copyout(&req, ifr->ifr_data, sizeof(req));
		break;
		
	case SIOCSVLLPARENT:
		copyin(ifr->ifr_data, &req, sizeof(req));

		if (req.parent[0] == '\0') {
			error = vll_detach(sc);
			break;
		}
		
		parent_ifp = ifunit(req.parent);

		if (parent_ifp == NULL) {
			error = EINVAL;
			break;
		}
		
		error = vll_attach(sc, parent_ifp);
		break;

	default:
		VLL_RLOCK(sc, &tracker);
		if (VLL_CONFIGURED(sc))
			error = sc->vll_ioctl(sc->vll_softc, cmd, data);
		else
			error = ether_ioctl(ifp, cmd, data);
		VLL_RUNLOCK(sc, &tracker);
		break;			
	}
	return (error);
}

static int
vll_transmit(struct ifnet *ifp, struct mbuf *m)
{
	struct vll_softc *sc;
	struct rm_priotracker tracker;
	int error;

	sc = ifp->if_softc;

	VLL_RLOCK(sc, &tracker);
	if (!VLL_CONFIGURED(sc)) {
		error = ENETDOWN;
		goto out;
	}
	
	error = sc->vll_transmit(sc->vll_softc, m);
out:
	VLL_RUNLOCK(sc, &tracker);
	return (error);
}

static void
vll_qflush(struct ifnet *ifp)
{
	struct vll_softc *sc;
	struct rm_priotracker tracker;

	sc = ifp->if_softc;

	VLL_RLOCK(sc, &tracker);
	if (VLL_CONFIGURED(sc))
		sc->vll_qflush(sc->vll_softc);
	VLL_RUNLOCK(sc, &tracker);
}


static int 
vll_clone_create(struct if_clone *ifc, char *name, size_t unit, caddr_t params)
{
	struct vllreq req;
	uint8_t null_addr[ETHER_ADDR_LEN];
	struct vll_softc *sc;
	struct ifnet *parent_ifp;
	struct ifnet *ifp;
	int error;

	bzero(null_addr, sizeof(null_addr));
	parent_ifp = NULL;
	error = 0;

	if (params != NULL) {
		error = copyin(params, &req, sizeof(req));

		if (error != 0)
			return (error);

		parent_ifp = ifunit(req.parent);
		if (parent_ifp == NULL)
			return (EINVAL);
	}

	sc = malloc(sizeof(*sc), M_VLL, M_WAITOK | M_ZERO);
	ifp = if_alloc(IFT_ETHER);

 	if (ifp == NULL) {
		free(sc, M_VLL);
		return (ENOSPC);
 	}

	sc->vll_ifp = ifp;
	sc->vll_parent = NULL;
	VLL_LOCK_INIT(sc);

	ifp->if_softc = sc;
	if_initname(ifp, VLL_NAME, unit);
	ifp->if_mtu = ETHERMTU;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_init = vll_init;
	ifp->if_ioctl = vll_ioctl;
	ifp->if_transmit = vll_transmit;
	ifp->if_qflush = vll_qflush;
	ifp->if_type = IFT_ETHER;
	ifp->if_capenable = 0;
	ifp->if_capabilities = 0;

	ether_ifattach(ifp, null_addr);

	if (parent_ifp != NULL) {
		error = vll_attach(sc, parent_ifp);

		if (error) {
			ether_ifdetach(ifp);
			if_free(ifp);
			VLL_LOCK_DESTROY(sc);
			free(sc, M_VLL);
		}
	}
	
	return (error);
}

static int 
vll_clone_destroy(struct if_clone *ifc, struct ifnet *ifp)
{
	struct vll_softc *sc;
	int error, unit;
	
	unit = ifp->if_dunit;
	sc = ifp->if_softc;

	error = vll_detach(sc);

	if (error)
		return (error);

	ether_ifdetach(ifp);
	if_free(ifp);

	VLL_LOCK_DESTROY(sc);
	free(sc, M_VLL);
	ifc_free_unit(ifc, unit);

	return (0);
}


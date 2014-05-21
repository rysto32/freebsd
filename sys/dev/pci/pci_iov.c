/*-
 * Copyright (c) 2013-2014 Sandvine Inc.  All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_bus.h"

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/fcntl.h>
#include <sys/ioccom.h>
#include <sys/iov.h>
#include <sys/linker.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/pciio.h>
#include <sys/queue.h>
#include <sys/rman.h>
#include <sys/sysctl.h>

#include <machine/bus.h>
#include <machine/stdarg.h>

#include <libkern/nv/nv.h>
#include <sys/iov_schema.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pci_private.h>
#include <dev/pci/pci_iov_private.h>

#include "pci_if.h"
#include "pcib_if.h"

static MALLOC_DEFINE(M_SRIOV, "sr_iov", "PCI SR-IOV allocations");

static d_ioctl_t pci_iov_ioctl;

static struct cdevsw iov_cdevsw = {
	.d_version = D_VERSION,
	.d_name = "iov",
	.d_ioctl = pci_iov_ioctl
};

SYSCTL_DECL(_hw_pci);

/*
 * The maximum amount of memory we will allocate for user configuration of an
 * SR-IOV device.  1MB ought to be enough for anyone, but leave this configuable
 * just in case.
 */
static u_long pci_iov_max_config = 1024 * 1024;
TUNABLE_ULONG("hw.pci.iov_max_config", &pci_iov_max_config);
SYSCTL_ULONG(_hw_pci, OID_AUTO, iov_max_config, CTLFLAG_RW, &pci_iov_max_config,
    0, "Maximum allowed size of SR-IOV configuration.");


#define IOV_READ(d, r, w) \
	pci_read_config((d)->cfg.dev, (d)->cfg.iov->iov_pos + r, w)

#define IOV_WRITE(d, r, v, w) \
	pci_write_config((d)->cfg.dev, (d)->cfg.iov->iov_pos + r, v, w)

typedef void (pci_iov_fill_schema_t)(device_t, nvlist_t *, nvlist_t *);

static pci_iov_fill_schema_t fill_driver_schema;
static pci_iov_fill_schema_t fill_iov_schema;

static nvlist_t *	pci_iov_get_schema(device_t dev);

int
pci_setup_iov_method(device_t bus, device_t dev)
{
	device_t pcib;
	struct pci_devinfo *dinfo;
	struct pcicfg_iov *iov;
	uint32_t version;
	int error;
	int iov_pos;

	dinfo = device_get_ivars(dev);
	pcib = device_get_parent(bus);
	
	error = pci_find_extcap(dev, PCIZ_SRIOV, &iov_pos);

	if (error != 0)
		return (error);

	version = pci_read_config(dev, iov_pos, 4); 
	if (PCI_EXTCAP_VER(version) != 1) {
		if (bootverbose)
			device_printf(dev, 
			    "Unsupported version of SR-IOV (%d) detected\n",
			    PCI_EXTCAP_VER(version));

		return (ENXIO);
	}

	iov = malloc(sizeof(*dinfo->cfg.iov), M_SRIOV, M_WAITOK | M_ZERO);

	mtx_lock(&Giant);
	if (dinfo->cfg.iov != NULL) {
		error = EBUSY;
		goto cleanup;
	}
	iov->iov_pos = iov_pos;

	iov->iov_cdev = make_dev(&iov_cdevsw, device_get_unit(dev),
	    UID_ROOT, GID_WHEEL, 0600, "iov/%s", device_get_nameunit(dev));

	if (iov->iov_cdev == NULL) {
		error = ENOMEM;
		goto cleanup;
	}
	
	dinfo->cfg.iov = iov;
	iov->iov_cdev->si_drv1 = dinfo;
	mtx_unlock(&Giant);

	return (0);

cleanup:
	free(iov, M_SRIOV);
	mtx_unlock(&Giant);
	return (error);
}

int
pci_cleanup_iov_method(device_t bus, device_t dev)
{
	struct pci_devinfo *dinfo;
	struct pcicfg_iov *iov;

	mtx_lock(&Giant);
	dinfo = device_get_ivars(dev);
	iov = dinfo->cfg.iov;

	if (iov == NULL) {
		mtx_unlock(&Giant);
		return (0);
	}

	if (iov->iov_num_vfs != 0) {
		mtx_unlock(&Giant);
		return (EBUSY);
	}

	dinfo->cfg.iov = NULL;

	if (iov->iov_cdev) {
		destroy_dev(iov->iov_cdev);
		iov->iov_cdev = NULL;
	}

	free(iov, M_SRIOV);
	mtx_unlock(&Giant);

	return (0);
}

static int
pci_iov_alloc_bar(struct pci_devinfo *dinfo, int bar, pci_addr_t bar_shift)
{
	struct resource *res;
	struct pcicfg_iov *iov;
	device_t dev, bus;
	u_long start, end;
	pci_addr_t bar_size;
	int rid;

	iov = dinfo->cfg.iov;
	dev = dinfo->cfg.dev;
	bus = device_get_parent(dev);
	rid = iov->iov_pos + PCIR_SRIOV_BAR(bar);
	bar_size = 1 << bar_shift;

	res = pci_alloc_multi_resource(bus, dev, SYS_RES_MEMORY, &rid, 1ul,
	    ~1ul, 1, iov->iov_num_vfs, RF_ACTIVE);

	if (res == NULL)
		return (ENXIO);

	iov->iov_bar[bar].res = res;
	iov->iov_bar[bar].bar_size = bar_size;
	iov->iov_bar[bar].bar_shift = bar_shift;

	start = rman_get_start(res);
	end = start + rman_get_size(res) - 1;
	return (rman_manage_region(&iov->rman, start, end));
}

static void
pci_iov_add_bars(struct pcicfg_iov *iov, struct pci_devinfo *dinfo)
{
	struct pci_iov_bar *bar;
	uint64_t bar_start;
	int i;

	for (i = 0; i <= PCIR_MAX_BAR_0; i++) {
		bar = &iov->iov_bar[i];
		if (bar->res != NULL) {
			bar_start = rman_get_start(bar->res) +
			    dinfo->cfg.vf.index * bar->bar_size;

			pci_add_bar(dinfo->cfg.dev, PCIR_BAR(i), bar_start,
			    bar->bar_shift);
		}
	}
}

static int
pci_iov_parse_config(device_t dev, struct pci_iov_arg *arg, nvlist_t **ret)
{
	void *packed_config;
	nvlist_t *schema, *config;
	int error;

	config = NULL;
	packed_config = NULL;
	schema = pci_iov_get_schema(dev);

	if (schema == NULL) {
		error = ENOMEM;
		goto out;
	}

	if (arg->len > pci_iov_max_config) {
		error = EMSGSIZE;
		goto out;
	}

	packed_config = malloc(arg->len, M_SRIOV, M_WAITOK);

	error = copyin(arg->config, packed_config, arg->len);
	if (error != 0)
		goto out;

	config = nvlist_unpack(packed_config, arg->len);
	if (config == NULL) {
		error = EINVAL;
		goto out;
	}

	error = pci_iov_schema_validate_config(schema, config);
	if (error != 0)
		goto out;

	error = nvlist_error(config);
	if (error != 0)
		goto out;

	*ret = config;
	config = NULL;

out:
	nvlist_destroy(schema);
	nvlist_destroy(config);
	free(packed_config, M_SRIOV);
	return (error);
}

/*
 * Set the ARI_EN bit in the lowest-numbered PCI function with the SR-IOV
 * capability.  This bit is only writeable on the lowest-numbered PF but
 * affects all PFs on the device.
 */
static int
pci_iov_set_ari(device_t bus)
{
	device_t lowest;
	device_t *devlist;
	int i, error, devcount, lowest_func, lowest_pos, iov_pos, dev_func;
	uint16_t iov_ctl;

	/* If ARI is disabled on the downstream port there is nothing to do. */
	if (!PCIB_ARI_ENABLED(device_get_parent(bus)))
		return (0);

	error = device_get_children(bus, &devlist, &devcount);

	if (error != 0)
		return (error);

	lowest = NULL;
	for (i = 0; i < devcount; i++) {
		if (pci_find_extcap(devlist[i], PCIZ_SRIOV, &iov_pos) == 0) {
			dev_func = pci_get_function(devlist[i]);
			if (lowest == NULL || dev_func < lowest_func) {
				lowest = devlist[i];
				lowest_func = dev_func;
				lowest_pos = iov_pos;
			}
		}
	}

	/*
	 * If we called this function some device must have the SR-IOV
	 * capability.
	 */
	KASSERT(lowest != NULL,
	    ("Could not find child of %s with SR-IOV capability",
	    device_get_nameunit(bus)));

	iov_ctl = pci_read_config(lowest, iov_pos + PCIR_SRIOV_CTL, 2);
	iov_ctl |= PCIM_SRIOV_ARI_EN;
	pci_write_config(lowest, iov_pos + PCIR_SRIOV_CTL, iov_ctl, 2);
	free(devlist, M_TEMP);
	return (0);
}

static int
pci_iov_config_page_size(struct pci_devinfo *dinfo)
{
	uint32_t page_cap, page_size;

	page_cap = IOV_READ(dinfo, PCIR_SRIOV_PAGE_CAP, 4);

	/*
	 * If the system page size is less than the smallest SR-IOV page size
	 * then round up to the smallest SR-IOV page size.
	 */
	if (PAGE_SHIFT < PCI_SRIOV_BASE_PAGE_SHIFT)
		page_size = (1 << 0);
	else
		page_size = (1 << (PAGE_SHIFT - PCI_SRIOV_BASE_PAGE_SHIFT));

	/* Check that the device supports the system page size. */
	if (!(page_size & page_cap))
		return (ENXIO);

	IOV_WRITE(dinfo, PCIR_SRIOV_PAGE_SIZE, page_size, 4);
	return (0);
}

static int
pci_init_iov(device_t dev, uint16_t num_vfs, const nvlist_t *config)
{
	const nvlist_t *device, *driver_config;

	device = nvlist_get_nvlist(config, PF_CONFIG_NAME);
	driver_config = nvlist_get_nvlist(device, DRIVER_CONFIG_NAME);
	return (PCI_INIT_IOV(dev, num_vfs, driver_config));
}

static int
pci_iov_init_rman(struct pcicfg_iov *iov)
{
	int error;

	iov->rman.rm_start = 0;
	iov->rman.rm_end = ~0ul;
	iov->rman.rm_type = RMAN_ARRAY;
	iov->rman.rm_descr = "SR-IOV VF I/O memory";

	error = rman_init(&iov->rman);
	if (error != 0)
		return (error);

	iov->iov_flags |= IOV_RMAN_INITED;
	return (0);
}

static int
pci_iov_setup_bars(struct pci_devinfo *dinfo)
{
	device_t dev;
	struct pcicfg_iov *iov;
	pci_addr_t bar_value, testval;
	int i, last_64, error;

	iov = dinfo->cfg.iov;
	dev = dinfo->cfg.dev;
	last_64 = 0;

	for (i = 0; i <= PCIR_MAX_BAR_0; i++) {
		/*
		 * If a PCI BAR is a 64-bit wide BAR, then it spans two
		 * consecutive registers.  Therefore if the last BAR that
		 * we looked at was a 64-bit BAR, we need to skip this
		 * register as it's the second half of the last BAR.
		 */
		if (!last_64) {
			pci_read_bar(dev,
			    iov->iov_pos + PCIR_SRIOV_BAR(i),
			    &bar_value, &testval, &last_64);

			if (testval != 0) {
				error = pci_iov_alloc_bar(dinfo, i,
				   pci_mapsize(testval));
				if (error != 0)
					return (error);
			}
		} else
			last_64 = 0;
	}

	return (0);
}

static void
pci_iov_enumerate_vfs(struct pci_devinfo *dinfo, const nvlist_t *config,
    uint16_t first_rid, uint16_t rid_stride)
{
	char device_name[VF_MAX_NAME];
	const nvlist_t *device, *driver_config, *iov_config;
	device_t bus, dev, vf;
	struct pcicfg_iov *iov;
	struct pci_devinfo *vfinfo;
	const char *driver;
	int i, error;
	uint16_t vid, did, next_rid;

	iov = dinfo->cfg.iov;
	dev = dinfo->cfg.dev;
	bus = device_get_parent(dev);
	next_rid = first_rid;
	vid = pci_get_vendor(dev);
	did = IOV_READ(dinfo, PCIR_SRIOV_VF_DID, 2);

	for (i = 0; i < iov->iov_num_vfs; i++, next_rid += rid_stride) {
		snprintf(device_name, sizeof(device_name), VF_PREFIX"%d", i);
		device = nvlist_get_nvlist(config, device_name);
		iov_config = nvlist_get_nvlist(device, IOV_CONFIG_NAME);
		driver_config = nvlist_get_nvlist(device, DRIVER_CONFIG_NAME);

		/*
		 * If we are creating passthrough devices then force the ppt
		 * driver to attach to prevent a VF driver from claiming the
		 * VFs.
		 */
		if (nvlist_get_bool(iov_config, "passthrough"))
			driver = "ppt";
		else
			driver = NULL;
		vf = pci_add_iov_child(bus, sizeof(*vfinfo), next_rid, vid, did,
		    driver);

		vfinfo = device_get_ivars(vf);

		vfinfo->cfg.iov = iov;
		vfinfo->cfg.vf.index = i;

		pci_iov_add_bars(iov, vfinfo);

		error = PCI_ADD_VF(dev, i, driver_config);
		if (error != 0) {
			device_printf(dev, "Failed to add VF %d\n", i);
			pci_delete_child(bus, vf);
		}
	}

	bus_generic_attach(bus);
}

static int
pci_iov_config(struct cdev *cdev, struct pci_iov_arg *arg)
{
	device_t bus, dev;
	struct pci_devinfo *dinfo;
	struct pcicfg_iov *iov;
	nvlist_t *config;
	int i, error;
	uint16_t rid_off, rid_stride;
	uint16_t first_rid, last_rid;
	uint16_t iov_ctl;
	uint16_t num_vfs, total_vfs;
	int iov_inited;

	mtx_lock(&Giant);
	dinfo = cdev->si_drv1;
	iov = dinfo->cfg.iov;
	dev = dinfo->cfg.dev;
	bus = device_get_parent(dev);
	iov_inited = 0;
	config = NULL;

	if ((iov->iov_flags & IOV_BUSY) || iov->iov_num_vfs != 0) {
		mtx_unlock(&Giant);
		return (EBUSY);
	}
	iov->iov_flags |= IOV_BUSY;

	error = pci_iov_parse_config(dev, arg, &config);
	if (error != 0)
		goto out;

	num_vfs = pci_iov_config_get_num_vfs(config);
	total_vfs = IOV_READ(dinfo, PCIR_SRIOV_TOTAL_VFS, 2);
	if (num_vfs > total_vfs) {
		error = EINVAL;
		goto out;
	}

	error = pci_iov_config_page_size(dinfo);
	if (error != 0)
		goto out;

	error = pci_iov_set_ari(bus);
	if (error != 0)
		goto out;

	error = pci_init_iov(dev, num_vfs, config);
	if (error != 0)
		goto out;
	iov_inited = 1;

	IOV_WRITE(dinfo, PCIR_SRIOV_NUM_VFS, num_vfs, 2);

	rid_off = IOV_READ(dinfo, PCIR_SRIOV_VF_OFF, 2);
	rid_stride = IOV_READ(dinfo, PCIR_SRIOV_VF_STRIDE, 2);

	first_rid = pci_get_rid(dev) + rid_off;
	last_rid = first_rid + (num_vfs - 1) * rid_stride;

	/* We don't yet support allocating extra bus numbers for VFs. */
	if (pci_get_bus(dev) != PCI_RID2BUS(last_rid)) {
		error = ENOSPC;
		goto out;
	}

	iov_ctl = IOV_READ(dinfo, PCIR_SRIOV_CTL, 2);
	iov_ctl &= ~(PCIM_SRIOV_VF_EN | PCIM_SRIOV_VF_MSE);
	IOV_WRITE(dinfo, PCIR_SRIOV_CTL, iov_ctl, 2);

	error = pci_iov_init_rman(iov);
	if (error != 0)
		goto out;

	iov->iov_num_vfs = num_vfs;

	error = pci_iov_setup_bars(dinfo);
	if (error != 0)
		goto out;

	iov_ctl = IOV_READ(dinfo, PCIR_SRIOV_CTL, 2);
	iov_ctl |= PCIM_SRIOV_VF_EN | PCIM_SRIOV_VF_MSE;
	IOV_WRITE(dinfo, PCIR_SRIOV_CTL, iov_ctl, 2);

	/* Per specification, we must wait 100ms before accessing VFs. */
	msleep(iov, &Giant, 0, "iov", hz/10);
	pci_iov_enumerate_vfs(dinfo, config, first_rid, rid_stride);

	nvlist_destroy(config);
	iov->iov_flags &= ~IOV_BUSY;
	mtx_unlock(&Giant);

	return (0);
out:
	if (iov_inited)
		PCI_UNINIT_IOV(dev);

	for (i = 0; i <= PCIR_MAX_BAR_0; i++) {
		if (iov->iov_bar[i].res != NULL) {
			pci_release_resource(bus, dev, SYS_RES_MEMORY,
			    iov->iov_pos + PCIR_SRIOV_BAR(i),
			    iov->iov_bar[i].res);
			pci_delete_resource(bus, dev, SYS_RES_MEMORY,
			    iov->iov_pos + PCIR_SRIOV_BAR(i));
			iov->iov_bar[i].res = NULL;
		}
	}

	if (iov->iov_flags & IOV_RMAN_INITED) {
		rman_fini(&iov->rman);
		iov->iov_flags &= ~IOV_RMAN_INITED;
	}

	nvlist_destroy(config);
	iov->iov_num_vfs = 0;
	iov->iov_flags &= ~IOV_BUSY;
	mtx_unlock(&Giant);
	return (error);
}

/* Return true if child is a VF of the given PF. */
static int
pci_iov_is_child_vf(struct pcicfg_iov *pf, device_t child)
{
	struct pci_devinfo *vfinfo;

	vfinfo = device_get_ivars(child);

	if (!(vfinfo->cfg.flags & PCICFG_VF))
		return (0);

	return (pf == vfinfo->cfg.iov);
}

static int
pci_iov_delete(struct cdev *cdev)
{
	device_t bus, dev, vf, *devlist;
	struct pci_devinfo *dinfo;
	struct pcicfg_iov *iov;
	int i, error, devcount;
	uint32_t iov_ctl;

	mtx_lock(&Giant);
	dinfo = cdev->si_drv1;
	iov = dinfo->cfg.iov;
	dev = dinfo->cfg.dev;
	bus = device_get_parent(dev);
	devlist = NULL;

	if (iov->iov_flags & IOV_BUSY) {
		mtx_unlock(&Giant);
		return (EBUSY);
	}

	if (iov->iov_num_vfs == 0) {
		mtx_unlock(&Giant);
		return (ECHILD);
	}

	iov->iov_flags |= IOV_BUSY;

	error = device_get_children(bus, &devlist, &devcount);

	if (error != 0)
		goto out;

	for (i = 0; i < devcount; i++) {
		vf = devlist[i];

		if (!pci_iov_is_child_vf(iov, vf))
			continue;

		error = device_detach(vf);
		if (error != 0) {
			/*
			 * If any device fails to detach, then re-attach all
			 * VFs to ensure that we leave things in the same state
			 * that we started in.
			 */
			bus_generic_attach(bus);
			goto out;
		}
	}

	for (i = 0; i < devcount; i++) {
		vf = devlist[i];

		if (pci_iov_is_child_vf(iov, vf))
			pci_delete_child(bus, vf);
	}
	PCI_UNINIT_IOV(dev);

	iov_ctl = IOV_READ(dinfo, PCIR_SRIOV_CTL, 2);
	iov_ctl &= ~(PCIM_SRIOV_VF_EN | PCIM_SRIOV_VF_MSE);
	IOV_WRITE(dinfo, PCIR_SRIOV_CTL, iov_ctl, 2);
	IOV_WRITE(dinfo, PCIR_SRIOV_NUM_VFS, 0, 2);

	iov->iov_num_vfs = 0;

	for (i = 0; i <= PCIR_MAX_BAR_0; i++) {
		if (iov->iov_bar[i].res != NULL) {
			pci_release_resource(bus, dev, SYS_RES_MEMORY,
			    iov->iov_pos + PCIR_SRIOV_BAR(i),
			    iov->iov_bar[i].res);
			pci_delete_resource(bus, dev, SYS_RES_MEMORY,
			    iov->iov_pos + PCIR_SRIOV_BAR(i));
			iov->iov_bar[i].res = NULL;
		}
	}

	if (iov->iov_flags & IOV_RMAN_INITED) {
		rman_fini(&iov->rman);
		iov->iov_flags &= ~IOV_RMAN_INITED;
	}

	error = 0;
out:
	free(devlist, M_TEMP);
	iov->iov_flags &= ~IOV_BUSY;
	mtx_unlock(&Giant);
	return (error);
}

static void
fill_driver_schema(device_t dev, nvlist_t *pf, nvlist_t *vf)
{

	PCI_GET_IOV_CONFIG_SCHEMA(dev, pf, vf);
}

static void
fill_iov_schema(device_t dev, nvlist_t *pf, nvlist_t *vf)
{

	/* VF parameters. */
	pci_iov_schema_add_bool(vf, "passthrough", IOV_SCHEMA_HASDEFAULT, 0);

	/* PF parameters. */
	pci_iov_schema_add_uint16(pf, "num_vfs", IOV_SCHEMA_REQUIRED, -1);
	pci_iov_schema_add_string(pf, "device", IOV_SCHEMA_REQUIRED, NULL);
}

/* Fill in the subsystem schema for both the PF and the VF. */
static int
pci_iov_fill_schema(device_t dev, nvlist_t *pf, nvlist_t *vf,
    const char *name, pci_iov_fill_schema_t *fill)
{
	nvlist_t *pf_sub, *vf_sub;

	pf_sub = nvlist_create(NV_FLAG_IGNORE_CASE);
	if (pf_sub == NULL)
		return (ENOMEM);

	vf_sub = nvlist_create(NV_FLAG_IGNORE_CASE);
	if (vf_sub == NULL) {
		nvlist_destroy(pf_sub);
		return (ENOMEM);
	}

	fill(dev, pf_sub, vf_sub);
	nvlist_move_nvlist(vf, name, vf_sub);
	nvlist_move_nvlist(pf, name, pf_sub);

	return (0);
}

static nvlist_t *
pci_iov_get_schema(device_t dev)
{
	nvlist_t *schema, *pf, *vf;
	int error;

	pf = NULL;
	vf = NULL;

	schema = nvlist_create(NV_FLAG_IGNORE_CASE);
	if (schema == NULL)
		goto fail;

	pf = nvlist_create(NV_FLAG_IGNORE_CASE);
	if (pf == NULL)
		goto fail;

	vf = nvlist_create(NV_FLAG_IGNORE_CASE);
	if (vf == NULL)
		goto fail;

	error = pci_iov_fill_schema(dev, pf, vf, IOV_CONFIG_NAME,
	    fill_iov_schema);
	if (error != 0)
		goto fail;

	error = pci_iov_fill_schema(dev, pf, vf, DRIVER_CONFIG_NAME,
	    fill_driver_schema);
	if (error != 0)
		goto fail;

	nvlist_move_nvlist(schema, PF_CONFIG_NAME, pf);
	nvlist_move_nvlist(schema, VF_SCHEMA_NAME, vf);

	return (schema);

fail:
	nvlist_destroy(schema);
	nvlist_destroy(pf);
	nvlist_destroy(vf);
	return (NULL);
}

static int
pci_iov_get_schema_ioctl(struct cdev *cdev, struct pci_iov_schema *output)
{
	device_t dev;
	struct pci_devinfo *dinfo;
	nvlist_t *schema;
	void *packed;
	size_t output_len, size;
	int error;

	schema = NULL;
	packed = NULL;

	mtx_lock(&Giant);
	dinfo = cdev->si_drv1;
	dev = dinfo->cfg.dev;

	schema = pci_iov_get_schema(dev);
	mtx_unlock(&Giant);

	if (schema == NULL) {
		error = ENOMEM;
		goto fail;
	}

	packed = nvlist_pack(schema, &size);
	if (packed == NULL) {
		error = nvlist_error(schema);
		if (error == 0)
			error = ENOMEM;
		goto fail;
	}

	output_len = output->len;
	output->len = size;
	if (size <= output_len) {
		error = copyout(packed, output->schema, size);

		if (error != 0)
			goto fail;

		output->error = 0;
	} else
		/*
		 * If we return an error then the ioctl code won't copyout
		 * output back to userland, so we flag the error in the struct
		 * instead.
		 */
		output->error = EMSGSIZE;

	error = 0;

fail:
	nvlist_destroy(schema);
	free(packed, M_NVLIST);

	return (error);
}

static int
pci_iov_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
{

	switch (cmd) {
	case IOV_CONFIG:
		return (pci_iov_config(dev, (struct pci_iov_arg *)data));
	case IOV_DELETE:
		return (pci_iov_delete(dev));
	case IOV_GET_SCHEMA:
		return (pci_iov_get_schema_ioctl(dev,
		    (struct pci_iov_schema *)data));
	default:
		return (EINVAL);
	}
}

struct resource *
pci_vf_alloc_mem_resource(device_t dev, device_t child, int *rid, u_long start,
    u_long end, u_long count, u_int flags)
{
	struct pci_devinfo *dinfo;
	struct pcicfg_iov *iov;
	struct pci_map *map;
	struct resource *res;
	struct resource_list_entry *rle;
	u_long bar_start, bar_end;
	pci_addr_t bar_length;

	dinfo = device_get_ivars(child);
	iov = dinfo->cfg.iov;

	map = pci_find_bar(child, *rid);
	if (map == NULL)
		return (NULL);

	bar_length = 1 << map->pm_size;
	bar_start = map->pm_value;
	bar_end = bar_start + bar_length - 1;

	res = rman_reserve_resource(&iov->rman, bar_start, bar_end,
	    bar_length, flags, child);

	if (res == NULL)
		return (NULL);

	rle = resource_list_add(&dinfo->resources, SYS_RES_MEMORY, *rid,
	    bar_start, bar_end, 1);

	if (rle == NULL) {
		rman_release_resource(res);
		return (NULL);
	}

	rle->res = res;
	rle->flags |= RLE_RESERVED;

	return (resource_list_alloc(&dinfo->resources, dev, child,
	    SYS_RES_MEMORY, rid, bar_start, bar_end, 1, flags));
}

int
pci_vf_release_mem_resource(device_t dev, device_t child, int rid,
    struct resource *r)
{
	struct pci_devinfo *dinfo;
	struct resource_list_entry *rle;

	dinfo = device_get_ivars(child);

	rle = resource_list_find(&dinfo->resources, SYS_RES_MEMORY, rid);

	if (rle != NULL) {
		rle->res = NULL;
		resource_list_delete(&dinfo->resources, SYS_RES_MEMORY,
		    rid);
	}

	return (rman_release_resource(r));
}


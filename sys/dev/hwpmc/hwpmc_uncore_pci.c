/*-
 * Copyright (c) 2013 Sandvine Inc.
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

/* Support for Uncore performance monitoring counters found in Intel Sandy
 * Bridge and later CPUs.  Unlike the uncore counters on previous chipsets,
 * these counters are accessed through PCI devices.
 */

#define UNC_PCI_IMC_NUM_BOXES 5		/* 1 fixed counter + 4 programmable */
#define UNC_PCI_COUNTER_WIDTH 48

#define	UNC_PCI_PMC_CAPS \
	(PMC_CAP_READ | PMC_CAP_WRITE)

#define UNC_MODEL_IB		(1 << 1)	/* Ivy Bridge */

#define UNC_FLAGS_UF		(1 << 0)	/* Uses fixed umask */


#define PMON_CTL_EN		(1 << 22)
#define PMON_CTL_UMASK_SHIFT	8
#define PMON_CTL_UMASK_MASK	(0xFF << PMON_CTL_UMASK_SHIFT)
#define PMON_CTL_EVENT_SHIFT	0
#define PMON_CTL_EVENT_MASK	(0xFF << PMON_CTL_EVENT_SHIFT)


enum unc_pci_boxes {
	UNC_PCI_UBOX,
	UNC_PCI_CBO,
	UNC_PCI_HA,
	UNC_IMC,
	UNC_IMCF,	/* iMC fixed counter */
};

#define UNC_PMCS() \
	UNC_PMC(RPQ_CYCLES_NE,	0x11,	IMC,	UNC_MODEL_IB,	UNC_FLAGS_UF, 1) \
	UNC_PMC(IMC_FIXED,	0,	IMCF,	UNC_MODEL_IB,	UNC_FLAGS_UF, 0) \

struct unc_pci_event {
	enum pmc_event pm_event;
	uint8_t pm_code;
	uint32_t pm_counter;
	uint32_t pm_models;
	uint32_t pm_flags;
	uint32_t pm_umask;
};

#define UNC_PMC(name, code, counter, models, flags, umask) \
	{ \
		.pm_event = PMC_EV_UCPCI_ ## name, \
		.pm_code = (code), \
		.pm_counter = UNC_ ## counter, \
		.pm_models = (models), \
		.pm_flags = (flags), \
		.pm_umask = (umask), \
	},
static struct unc_pci_event unc_pci_events[] = {
	UNC_PMCS()
};
#undef UNC_PMC

static const int unc_pci_npmc = nitems(unc_pci_events);

struct unc_pci_cpu {
	struct pmc_hw		pc_uncorepmcs[];
};

struct unc_pci_cpu **unc_pci_pcpu;


static int
unc_pci_allocate_pmc(int cpu, int ri, struct pmc *pm,
    const struct pmc_op_pmcallocate *a)
{
	int n;
	struct unc_pci_event *ie;
	uint32_t modelflag, cntrctl;

	/* Ensure that they aren't requesting unsupported capabilities. */
	if ((a->pm_caps & ~UNC_PCI_PMC_CAPS) != 0)
		return (EPERM);

	modelflag = UNC_MODEL_IB;

	for (n = 0, ie = ucp_events; n < nucp_events; n++, ie++)
		if (ie->pm_event == pm->pm_event && ie->pm_models & modelflag)
			break;

	if (n == unc_pci_npmc)
		return (EINVAL);

	if (ri == 0) {
		if (ie->pm_counter != UNC_IMC_FIXED)
			return (EINVAL);

	} else {
		if (ie->pm_counter != UNC_IMC)
			return (EINVAL);
	}

	cntrctl = PMON_CTL_EN;
	cntrctl |= ie->pm_event << PMON_CTL_EVENT_SHIFT;

	if (ie->pm_flags & UNC_FLAGS_UF)
		cntrctl |= ie->pm_umask << PMON_CTL_UMASK_SHIFT;
	else
		return (EDOOFUS);

	pm->pm_md.pm_ucpci.cntr_ctl = cntrctl;

	return (0);
}

static int
unc_pci_config_pmc(int cpu, int ri, struct pmc *pm)
{
	KASSERT(ri >= 0 && ri < UNC_PCI_IMC_NUM_BOXES,
	    ("row index %d is out of range", ri));

	unc_pci_pcpu[cpu]->pc_pmcs[ri].phw_pmc = pm;
	return (0);
}

static int
unc_pci_describe(int cpu, int ri, struct pmc_info *pi, struct pmc **ppmc)
{
	struct pmc_hw *phw;

	phw = &unc_pci_pcpu[cpu]->pc_uncorepmcs[ri];

	if (ri == 0)
		strlcpy(pi->pm_name, "IMCF", sizeof(pi->pm_name));
	else
		snprintf(pi->pm_name, sizeof(pi->pm_name), "IMC-%d", ri);

	pi->pm_class = PCM_CLASS_UNC_IMC;

	if (phw->phw_state & PMC_PHW_FLAG_IS_ENABLED) {
		pi->pm_enabled = TRUE;
		*ppmc          = phw->phw_pmc;
	} else {
		pi->pm_enabled = FALSE;
		*ppmc          = NULL;
	}

	return (0);
}

static int
unc_pci_get_config(int cpu, int ri, struct pmc **pmc)
{

	*pmc = unc_pci_pcpu[cpu]->pc_uncorepmcs[ri].phw_pmc;

	return (0);
}

static int
unc_pci_pcpu_fini(struct pmc_mdep *md, int cpu)
{
}

static int
unc_pci_pcpu_init(struct pmc_mdep *md, int cpu)
{
}

static int
unc_pci_read_pmc(int cpu, int ri, pmc_value_t *v)
{
}

static int
unc_pci_release_pmc(int cpu, int ri, struct pmc *pm)
{
}

static int
unc_pci_start_pmc(int cpu, int ri)
{
}

static int
unc_pci_stop_pmc(int cpu, int ri)
{
}

static int
unc_pci_write_pmc(int cpu, int ri, pmc_value_t v)
{
}

int
pmc_uncore_pci_initialize(struct pmc_mdep *md, int ncpus)
{
	struct pmc_classdep *pcd;

	KASSERT(md != NULL, ("[ucp,%d] md is NULL", __LINE__));

	PMCDBG(MDP,INI,1, "%s", "ucp-initialize");

	pcd = &md->pmd_classdep[PMC_MDEP_CLASS_INDEX_UCPCI];

	pcd->pcd_caps	= UNC_PCI_PMC_CAPS;
	pcd->pcd_class	= PMC_CLASS_UNC_PCI;
	pcd->pcd_num	= UNC_PCI_IMC_NUM_BOXES;
	pcd->pcd_ri	= md->pmd_npmc;
	pcd->pcd_width	= UCPCI_COUNTER_WIDTH;

	pcd->pcd_allocate_pmc	= unc_pci_allocate_pmc;
	pcd->pcd_config_pmc	= unc_pci_config_pmc;
	pcd->pcd_describe	= unc_pci_describe;
	pcd->pcd_get_config	= unc_pci_get_config;
	pcd->pcd_get_msr	= NULL;
	pcd->pcd_pcpu_fini	= unc_pci_pcpu_fini;
	pcd->pcd_pcpu_init	= unc_pci_pcpu_init;
	pcd->pcd_read_pmc	= unc_pci_read_pmc;
	pcd->pcd_release_pmc	= unc_pci_release_pmc;
	pcd->pcd_start_pmc	= unc_pci_start_pmc;
	pcd->pcd_stop_pmc	= unc_pci_stop_pmc;
	pcd->pcd_write_pmc	= unc_pci_write_pmc;

	md->pmd_npmc	       += UNC_PCI_IMC_NUM_BOXES;

	unc_pci_pcpu = malloc (sizeof(struct unc_pci_cpu) * ncpus, M_PMC, 
	    M_ZERO | M_WAITOK);
	
}

void
pmc_uncore_pci_finialize(struct pmc_mdep *md)
{

	free(unc_pci_pcpu, M_PMC);
	unc_pci_pcpu = NULL;
}

 

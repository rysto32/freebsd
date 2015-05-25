/******************************************************************************

  Copyright (c) 2013-2015, Intel Corporation 
  All rights reserved.
  
  Redistribution and use in source and binary forms, with or without 
  modification, are permitted provided that the following conditions are met:
  
   1. Redistributions of source code must retain the above copyright notice, 
      this list of conditions and the following disclaimer.
  
   2. Redistributions in binary form must reproduce the above copyright 
      notice, this list of conditions and the following disclaimer in the 
      documentation and/or other materials provided with the distribution.
  
   3. Neither the name of the Intel Corporation nor the names of its 
      contributors may be used to endorse or promote products derived from 
      this software without specific prior written permission.
  
  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE 
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.

******************************************************************************/
/*$FreeBSD$*/


#ifndef _IXLV_H_
#define _IXLV_H_

#include "ixlv_vc_mgr.h"


#define IXLV_AQ_MAX_ERR		1000
#define IXLV_MAX_FILTERS	128
#define IXLV_AQ_TIMEOUT		(1 * hz)
#define IXLV_CALLOUT_TIMO	(hz / 50)	/* 20 msec */
#define IXLV_RESET_TIMO		(1 * hz)	/* 1 sec */

#define IXLV_FLAG_AQ_ENABLE_QUEUES		0
#define IXLV_FLAG_AQ_DISABLE_QUEUES		1
#define IXLV_FLAG_AQ_ADD_MAC_FILTER		2
#define IXLV_FLAG_AQ_ADD_VLAN_FILTER		3
#define IXLV_FLAG_AQ_DEL_MAC_FILTER		4
#define IXLV_FLAG_AQ_DEL_VLAN_FILTER		5
#define IXLV_FLAG_AQ_CONFIGURE_QUEUES		6
#define IXLV_FLAG_AQ_MAP_VECTORS		7
#define IXLV_FLAG_AQ_HANDLE_RESET		8
#define IXLV_FLAG_AQ_CONFIGURE_PROMISC		9
#define IXLV_FLAG_AQ_GET_STATS			10
#define IXLV_FLAG_AQ_SET_UDP_PRIO		11

/* Hack for compatibility with 1.0.x linux pf driver */
#define I40E_VIRTCHNL_OP_EVENT 17

/* Driver state */
enum ixlv_state_t {
	IXLV_START,
	IXLV_FAILED,
	IXLV_RESET_REQUIRED,
	IXLV_RESET_PENDING,
	IXLV_INIT_RESET_DONE,
	IXLV_STOPPED,
	IXLV_INIT_START,
	IXLV_INIT_COMPLETE,
	IXLV_RUNNING,
};

struct ixlv_mac_filter {
	SLIST_ENTRY(ixlv_mac_filter)  next;
	u8      macaddr[ETHER_ADDR_LEN];
	u16     flags;
};
SLIST_HEAD(mac_list, ixlv_mac_filter);

struct ixlv_vlan_filter {
	SLIST_ENTRY(ixlv_vlan_filter)  next;
	u16     vlan;
	u16     flags;
};
SLIST_HEAD(vlan_list, ixlv_vlan_filter);

/* Flags for ixlv_sc.vc_flags. */
#define IXLV_VC_FLAG_QUEUES_EN		0x0001

/* Software controller structure */
struct ixlv_sc {
	struct i40e_hw		hw;
	struct i40e_osdep	osdep;
	struct device		*dev;

	struct resource		*pci_mem;
	struct resource		*msix_mem;

	enum ixlv_state_t	init_state;
	int			reset_start;

	/*
	 * Interrupt resources
	 */
	void			*tag;
	struct resource 	*res; /* For the AQ */

	struct ifmedia		media;
	struct callout		timer;
	int			msix;
	int			if_flags;
	int			detaching;

	bool			link_up;
	u32			link_speed;

	struct mtx		mtx;

	u32			qbase;
	u32 			admvec;
	struct callout		timeout;
	struct task     	aq_irq;
	struct task     	aq_sched;
	struct task		init_task;
	struct taskqueue	*tq;

	struct ixl_vsi		vsi;

	/* Filter lists */
	struct mac_list		*mac_filters;
	struct vlan_list	*vlan_filters;

	/* Promiscuous mode */
	u32			promiscuous_flags;

	/* Admin queue task flags */
	u32			aq_wait_count;
	u32			aq_inited;

	struct ixl_vc_mgr	vc_mgr;
	struct ixl_vc_cmd	disable_queues_cmd;
	struct ixl_vc_cmd	add_mac_cmd;
	struct ixl_vc_cmd	del_mac_cmd;
	struct ixl_vc_cmd	config_queues_cmd;
	struct ixl_vc_cmd	map_vectors_cmd;
	struct ixl_vc_cmd	enable_queues_cmd;
	struct ixl_vc_cmd	add_vlan_cmd;
	struct ixl_vc_cmd	del_vlan_cmd;
	struct ixl_vc_cmd	add_multi_cmd;
	struct ixl_vc_cmd	del_multi_cmd;

	/* Virtual comm channel */
	struct i40e_virtchnl_vf_resource *vf_res;
	struct i40e_virtchnl_vsi_resource *vsi_res;

	/* Misc stats maintained by the driver */
	u64			watchdog_events;
	u64			admin_irq;

	u8			aq_buffer[IXL_AQ_BUF_SZ];

	uint32_t		vc_flags;
};

#define IXLV_CORE_LOCK(sc)		mtx_lock(&(sc)->mtx)
#define IXLV_CORE_UNLOCK(sc)		mtx_unlock(&(sc)->mtx)
#define IXLV_CORE_LOCK_ASSERT(sc)	mtx_assert(&(sc)->mtx, MA_OWNED)
/*
** This checks for a zero mac addr, something that will be likely
** unless the Admin on the Host has created one.
*/
static inline bool
ixlv_check_ether_addr(u8 *addr)
{
	bool status = TRUE;

	if ((addr[0] == 0 && addr[1]== 0 && addr[2] == 0 &&
	    addr[3] == 0 && addr[4]== 0 && addr[5] == 0))
		status = FALSE;
	return (status);
}

/*
** VF Common function prototypes
*/
int	ixlv_send_api_ver(struct ixlv_sc *);
int	ixlv_verify_api_ver(struct ixlv_sc *);
int	ixlv_send_vf_config_msg(struct ixlv_sc *);
int	ixlv_get_vf_config(struct ixlv_sc *);
void	ixlv_init(void *);
void	ixlv_init_locked(struct ixlv_sc *sc);
void	ixlv_start_reset(struct ixlv_sc *sc, int delay);
void	ixlv_configure_queues(struct ixlv_sc *);
void	ixlv_enable_queues(struct ixlv_sc *);
void	ixlv_disable_queues(struct ixlv_sc *);
void	ixlv_map_queues(struct ixlv_sc *);
void	ixlv_enable_intr(struct ixl_vsi *);
void	ixlv_disable_intr(struct ixl_vsi *);
void	ixlv_add_ether_filters(struct ixlv_sc *);
void	ixlv_del_ether_filters(struct ixlv_sc *);
void	ixlv_request_stats(struct ixlv_sc *);
void	ixlv_request_reset(struct ixlv_sc *);
void	ixlv_vc_completion(struct ixlv_sc *,
	enum i40e_virtchnl_ops, i40e_status, u8 *, u16);
void	ixlv_add_ether_filter(struct ixlv_sc *);
void	ixlv_add_vlans(struct ixlv_sc *);
void	ixlv_del_vlans(struct ixlv_sc *);
void	ixlv_update_stats_counters(struct ixlv_sc *,
		    struct i40e_eth_stats *);
void	ixlv_update_link_status(struct ixlv_sc *);


#endif /* _IXLV_H_ */

/*-
 * Copyright (c) 2001-2002 Luigi Rizzo
 *
 * Supported by: the Xorp Project (www.xorp.org)
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_device_polling.h"
#include "opt_kdtrace.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/proc.h>
#include <sys/eventhandler.h>
#include <sys/resourcevar.h>
#include <sys/sdt.h>
#include <sys/socket.h>			/* needed by net/if.h		*/
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/netisr.h>			/* for NETISR_POLL		*/
#include <net/vnet.h>

void hardclock_device_poll(void);	/* hook from hardclock		*/

static struct mtx	poll_mtx;

enum poller_phase {
	POLLER_SLEEPING, POLLER_POLL, POLLER_POLL_DONE, POLLER_POLLMORE
};

/*
 * Polling support for [network] device drivers.
 *
 * Drivers which support this feature can register with the
 * polling code.
 *
 * If registration is successful, the driver must disable interrupts,
 * and further I/O is performed through the handler, which is invoked
 * (at least once per clock tick) with 3 arguments: the "arg" passed at
 * register time (a struct ifnet pointer), a command, and a "count" limit.
 *
 * The command can be one of the following:
 *  POLL_ONLY: quick move of "count" packets from input/output queues.
 *  POLL_AND_CHECK_STATUS: as above, plus check status registers or do
 *	other more expensive operations. This command is issued periodically
 *	but less frequently than POLL_ONLY.
 *
 * The count limit specifies how much work the handler can do during the
 * call -- typically this is the number of packets to be received, or
 * transmitted, etc. (drivers are free to interpret this number, as long
 * as the max time spent in the function grows roughly linearly with the
 * count).
 *
 * Polling is enabled and disabled via setting IFCAP_POLLING flag on
 * the interface. The driver ioctl handler should register interface
 * with polling and disable interrupts, if registration was successful.
 *
 * A second variable controls the sharing of CPU between polling/kernel
 * network processing, and other activities (typically userlevel tasks):
 * kern.polling.user_frac (between 0 and 100, default 50) sets the share
 * of CPU allocated to user tasks. CPU is allocated proportionally to the
 * shares, by dynamically adjusting the "count" (poll_each_burst).
 */

SDT_PROVIDER_DEFINE(device_polling);

/*
 * Probe called just before hardclock calls schedules this tick's set of
 * polling iterations.
 *
 * Arguments:
 *      0 - Poller index.  Currently always 0.
 *      1 - Current time in us.  Value can wrap, ticks-style.
 *      2 - The current poller phase.
 */
SDT_PROBE_DEFINE3(device_polling,,, netisr_sched, "int", "int", "int");

/*
 * Probe called before netisr_pollmore decides whether to schedule another
 * iteration of polling.
 *
 * Arguments:
 *       0 - Poller index.  Currently always 0.
 *       1 - Current time in us.
 *       2 - Average ns per packet handled in an iteration.
 *       3 - Current value of polling_done.  If 0, pollmore will not schedule
 *           another iteration.
 */
SDT_PROBE_DEFINE4(device_polling,,, first_params, "int", "int", "int", "int");

/*
 * Probe called before netisr_pollmore decides whether to schedule another
 * iteration of polling.  This is called at right after first_params.  The
 * probe is split into two because I wanted to pass more than 5 arguments.
 *
 * Arguments:
 *       0 - Poller index.  Currently always 0.
 *       1 - The value of ticks when this polling iteration started.
 *       2 - The value of ticks now.
 */
SDT_PROBE_DEFINE3(device_polling,,, second_params, "int", "int", "int");

/*
 * Probe called when netisr_pollmore is going to schedule another iteration.
 *
 * Arguments:
 *       0 - Poller index.  Currently always 0.
 */
SDT_PROBE_DEFINE1(device_polling,,, reschedule, "int");

/*
 * Probe called when netisr_pollmore is not going to schedule another iteration
 * and the poller is going to "sleep" until the next tick.
 *
 * Arguments:
 *       0 - Poller index.  Currently always 0.
 *       1 - Value of polling_done.
 *       2 - 1 if we have not crossed a tick boundary.
 */
SDT_PROBE_DEFINE3(device_polling,,, sleep, "int", "int", "int");

/*
 * Probe called on the first polling iteration of a tick.
 *
 * Arguments:
 *       0 - Poller index.  Currently always 0.
 *       1 - Time, in us, at which we must stop polling because we run out of
 *           CPU time allocated to netisr.
 */
SDT_PROBE_DEFINE2(device_polling,,, first_poll, "int", "int");

/*
 * Probe called after the poller has calculated how many packets to handle in
 * this iteration.
 *
 * Arguments:
 *       0 - Poller index.  Currently always 0.
 *       1 - Number of packets that we have time to handle.
 *       2 - Number of packets that we are going to handle in this iteration.
 */
SDT_PROBE_DEFINE3(device_polling,,, poll_cycles, "int", "int", "int");

/*
 * Probe called a polling handler is called.
 *
 * Arguments:
 *       0 - Poller index.  Currently always 0.
 *       1 - Function pointer to the poller handler.
 *       2 - Pointer to the ifnet being polled.
 */
SDT_PROBE_DEFINE3(device_polling,,, before_poller, "int", "void *",
    "struct ifnet *");

/*
 * Probe called after a polling handler returns.
 *
 * Arguments:
 *       0 - Poller index.  Currently always 0.
 *       1 - Function pointer to the poller handler.
 *       2 - Pointer to the ifnet that was polled.
 *       3 - The number of packets handled by the ifnet in this iteration.
 *       4 - The maximum number of packets we told the ifnet to handle.
 */
SDT_PROBE_DEFINE5(device_polling,,, after_poller, "int", "void *",
    "struct ifnet *", "int", "int");

/*
 * Probe called after netisr_poll has called into every poller.
 *
 * Arguments:
 *       0 - Poller index.  Currently always 0.
 *       1 - Largest number of packets handled by a handler in this iteration.
 *       2 - Maximum number of packets we told handlers to handle.
 *       3 - 1 if no handler did enough work to warrant another iteration in
 *           this tick.
 *       4 - The values of ticks at the start of this set of iterations.
 */
SDT_PROBE_DEFINE5(device_polling,,, pollers_done, "int", "int", "int", "int",
    "int");

static uint32_t poll_each_burst = 30;

static SYSCTL_NODE(_kern, OID_AUTO, polling, CTLFLAG_RW, 0,
	"Device polling parameters");

static int	poll_shutting_down;

static int poll_each_burst_sysctl(SYSCTL_HANDLER_ARGS)
{
	uint32_t val = poll_each_burst;
	int error;

	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error || !req->newptr )
		return (error);
	if (val < 1)
		return (EINVAL);

	mtx_lock(&poll_mtx);
	poll_each_burst = val;
	mtx_unlock(&poll_mtx);

	return (0);
}
SYSCTL_PROC(_kern_polling, OID_AUTO, each_burst, CTLTYPE_UINT | CTLFLAG_RW,
	0, sizeof(uint32_t), poll_each_burst_sysctl, "I",
	"Max size of each burst");

static uint32_t poll_in_idle_loop=0;	/* do we poll in idle loop ? */
SYSCTL_UINT(_kern_polling, OID_AUTO, idle_poll, CTLFLAG_RW,
	&poll_in_idle_loop, 0, "Enable device polling in idle loop");

static uint32_t user_frac = 50;
static int user_frac_sysctl(SYSCTL_HANDLER_ARGS)
{
	uint32_t val = user_frac;
	int error;

	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error || !req->newptr )
		return (error);
	if (val > 99)
		return (EINVAL);

	mtx_lock(&poll_mtx);
	user_frac = val;
	mtx_unlock(&poll_mtx);

	return (0);
}
SYSCTL_PROC(_kern_polling, OID_AUTO, user_frac, CTLTYPE_UINT | CTLFLAG_RW,
	0, sizeof(uint32_t), user_frac_sysctl, "I",
	"Desired user fraction of cpu time");

static uint32_t reg_frac_count = 0;
static uint32_t reg_frac = 20 ;
static int reg_frac_sysctl(SYSCTL_HANDLER_ARGS)
{
	uint32_t val = reg_frac;
	int error;

	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error || !req->newptr )
		return (error);
	if (val < 1 || val > hz)
		return (EINVAL);

	mtx_lock(&poll_mtx);
	reg_frac = val;
	if (reg_frac_count >= reg_frac)
		reg_frac_count = 0;
	mtx_unlock(&poll_mtx);

	return (0);
}
SYSCTL_PROC(_kern_polling, OID_AUTO, reg_frac, CTLTYPE_UINT | CTLFLAG_RW,
	0, sizeof(uint32_t), reg_frac_sysctl, "I",
	"Every this many cycles check registers");

static uint32_t short_ticks;
SYSCTL_UINT(_kern_polling, OID_AUTO, short_ticks, CTLFLAG_RD,
	&short_ticks, 0, "Hardclock ticks shorter than they should be");

static uint32_t lost_polls;
SYSCTL_UINT(_kern_polling, OID_AUTO, lost_polls, CTLFLAG_RD,
	&lost_polls, 0, "How many times we would have lost a poll tick");

static uint32_t pending_polls;
SYSCTL_UINT(_kern_polling, OID_AUTO, pending_polls, CTLFLAG_RD,
	&pending_polls, 0, "Do we need to poll again");

static uint32_t poll_handlers; /* next free entry in pr[]. */
SYSCTL_UINT(_kern_polling, OID_AUTO, handlers, CTLFLAG_RD,
	&poll_handlers, 0, "Number of registered poll handlers");

static enum poller_phase poll_phase;
SYSCTL_INT(_kern_polling, OID_AUTO, phase, CTLFLAG_RD,
	&poll_phase, 0, "Polling phase");

static uint32_t suspect;
SYSCTL_UINT(_kern_polling, OID_AUTO, suspect, CTLFLAG_RD,
	&suspect, 0, "suspect event");

static uint32_t stalled;
SYSCTL_UINT(_kern_polling, OID_AUTO, stalled, CTLFLAG_RD,
	&stalled, 0, "potential stalls");

static uint32_t idlepoll_sleeping; /* idlepoll is sleeping */
SYSCTL_UINT(_kern_polling, OID_AUTO, idlepoll_sleeping, CTLFLAG_RD,
	&idlepoll_sleeping, 0, "idlepoll is sleeping");

static int poll_min_reschedule = 2;
SYSCTL_INT(_kern_polling, OID_AUTO, min_reschedule, CTLFLAG_RW,
    &poll_min_reschedule, 0,
    "minimum number of packets to handle in a polling iteration");

static int poll_ns_per_count;
SYSCTL_INT(_kern_polling, OID_AUTO, ns_per_packet, CTLFLAG_RD,
    &poll_ns_per_count, 0,
    "Average number of nanoseconds required to handle 1 packet");

static int poll_end_usec;
static int poll_done_usec;
static int polling_done;
static int poll_ticks_at_start;
static int poll_tick_packets;
static int last_hardclock;

#define POLL_LIST_LEN  128
struct pollrec {
	poll_handler_t	*handler;
	struct ifnet	*ifp;
};

static struct pollrec pr[POLL_LIST_LEN];

static void
poll_shutdown(void *arg, int howto)
{

	poll_shutting_down = 1;
}

static void
init_device_poll(void)
{

	mtx_init(&poll_mtx, "polling", NULL, MTX_DEF);
	EVENTHANDLER_REGISTER(shutdown_post_sync, poll_shutdown, NULL,
	    SHUTDOWN_PRI_LAST);
}
SYSINIT(device_poll, SI_SUB_SOFTINTR, SI_ORDER_MIDDLE, init_device_poll, NULL);

/*
 * Exponentially weighted moving average constants for smoothing ns_per_count.
 * a = POLL_NS_AVG_OLD / POLL_NS_AVG_DEN.  X represents the sequence of
 * measured ns_per_count values and y is the sequence of averaged values.
 *
 * y  = (1-a) x  + a y
 *  k          k      k-1
 */
#define	POLL_NS_AVG_OLD	64
#define	POLL_NS_AVG_DEN	128
#define	POLL_NS_AVG_NEW	(POLL_NS_AVG_DEN - POLL_NS_AVG_OLD)

/* Convert timeval into ticks-style wrapping around signed int. */
static __inline int
tv_to_usec(struct timeval tv)
{
	return ((int)(tv.tv_sec * 1000000 + tv.tv_usec));
}

/*
 * Hook from hardclock. Tries to schedule a netisr, but keeps track
 * of lost ticks due to the previous handler taking too long.
 * Normally, this should not happen, because polling handler should
 * run for a short time. However, in some cases (e.g. when there are
 * changes in link status etc.) the drivers take a very long time
 * (even in the order of milliseconds) to reset and reconfigure the
 * device, causing apparent lost polls.
 *
 * The first part of the code is just for debugging purposes, and tries
 * to count how often hardclock ticks are shorter than they should,
 * meaning either stray interrupts or delayed events.
 */
void
hardclock_device_poll(void)
{
	static int prev_usec;
	struct timeval t;
	int usec;
	int delta;
	int pending;

	if (poll_handlers == 0 || poll_shutting_down)
		return;

	microuptime(&t);
	usec = tv_to_usec(t);
	delta = usec - prev_usec;
	if (delta * hz < 500000)
		short_ticks++;
	else
		prev_usec = usec;

	pending = atomic_fetchadd_int(&pending_polls, 1);
	if (pending > 100) {
		stalled++;
		pending_polls = 0;
	} else if (pending > 0)
		lost_polls++;

	if (poll_phase != POLLER_SLEEPING)
		suspect++;

	last_hardclock = usec;
	SDT_PROBE3(device_polling,,, netisr_sched, 0, usec, poll_phase);
	netisr_sched_poll();
}

/*
 * ether_poll is called from the idle loop.
 */
static void
ether_poll(int count)
{
	int i;

	mtx_lock(&poll_mtx);

	if (count > poll_each_burst)
		count = poll_each_burst;

	for (i = 0 ; i < poll_handlers ; i++)
		pr[i].handler(pr[i].ifp, POLL_ONLY, count);

	mtx_unlock(&poll_mtx);
}

/*
 * netisr_pollmore is called after other netisr's, possibly scheduling
 * another NETISR_POLL call, or adapting the burst size for the next cycle.
 *
 * It is very bad to fetch large bursts of packets from a single card at once,
 * because the burst could take a long time to be completely processed, or
 * could saturate the intermediate queue (ipintrq or similar) leading to
 * losses or unfairness. To reduce the problem, and also to account better for
 * time spent in network-related processing, we split the burst in smaller
 * chunks of fixed size, giving control to the other netisr's between chunks.
 * This helps in improving the fairness, reducing livelock (because we
 * emulate more closely the "process to completion" that we have with
 * fastforwarding) and accounting for the work performed in low level
 * handling and forwarding.
 */

static int poll_start_usec;

void
netisr_pollmore(void)
{
	struct timeval now;
	int usec;
	int ticks_now;

	if (poll_handlers == 0)
		return;

	mtx_lock(&poll_mtx);
	poll_phase = POLLER_POLLMORE;

	/* here we can account time spent in netisr's in this tick */
	microuptime(&now);
	usec = tv_to_usec(now);

	ticks_now = ticks;

	SDT_PROBE4(device_polling,,, first_params, 0, usec, poll_ns_per_count, polling_done);
	SDT_PROBE3(device_polling,,, second_params, 0, poll_ticks_at_start, ticks_now);

	/*
	 * Schedule another poll if we have enough time to do at least
	 * poll_min_reschedule packets, if we aren't done polling for lack of
	 * work to do and if we haven't crossed a tick boundary(if we've crossed
	 * a tick boundary there is a pending wakeup waiting from hardclock
	 * which kick off this tick's round of polling).
	 */
	if ((poll_end_usec - usec) * 1000 >
	    poll_ns_per_count * poll_min_reschedule &&
	    !polling_done && poll_ticks_at_start == ticks_now) {
		polling_done = 0;
		netisr_sched_poll();

		SDT_PROBE1(device_polling,,, reschedule, 0);
	} else {
		SDT_PROBE3(device_polling,,, sleep, 0, polling_done,
		    poll_ticks_at_start == ticks_now);

		polling_done = 0;
		pending_polls = 0;
		poll_phase = POLLER_SLEEPING;
		poll_done_usec = usec;
	}
	mtx_unlock(&poll_mtx);
}

/*
 * netisr_poll is typically scheduled once per tick.
 */
void
netisr_poll(void)
{
	struct timeval now_t;
	int i, cycles;
	enum poll_cmd arg = POLL_ONLY;
	int rxcount, remaining_usec, max_rx, deltausec;
	int newnsper, residual_burst, now_usec;

	if (poll_handlers == 0)
		return;

	mtx_lock(&poll_mtx);
	microuptime(&now_t);
	now_usec = tv_to_usec(now_t);
	if (poll_phase == POLLER_SLEEPING) { /* first call in this tick */
		atomic_subtract_int(&pending_polls, 1);

		if (poll_tick_packets > 0) {
			deltausec = poll_done_usec - poll_start_usec;
			newnsper = deltausec * 1000 / poll_tick_packets;

			poll_ns_per_count = (newnsper * POLL_NS_AVG_NEW
			    + poll_ns_per_count * POLL_NS_AVG_OLD)
			    / POLL_NS_AVG_DEN;
			poll_tick_packets = 0;
		}
		poll_start_usec = now_usec;
		poll_ticks_at_start = ticks;
		if (++reg_frac_count == reg_frac) {
			arg = POLL_AND_CHECK_STATUS;
			reg_frac_count = 0;
		}

		/*
		 * 10000 = 1000000 us/s divided by 100 to convert user_frac to a
		 * percentage.
		 */
		poll_end_usec = last_hardclock + (100 - user_frac) * 10000 / hz;

		SDT_PROBE2(device_polling,,, first_poll, 0, poll_end_usec);
	}
	poll_phase = POLLER_POLL;
	remaining_usec = poll_end_usec - now_usec;

	/*
	 * If poll_ns_per_count is invalid replace it with an arbitrary,
	 * conservative value.
	 */
	if (poll_ns_per_count <= 0)
		poll_ns_per_count = 50000;

	/* Calculate how many more packets we have time to do in this tick. */
	residual_burst = remaining_usec * 1000 / poll_ns_per_count;
	if (residual_burst <= 0)
		residual_burst = 1;
	/*
	 * Don't do more than poll_each_burst packets in this iteration.  If
	 * there's a lot of packets waiting we'll get them in the next
	 * iteration.
	 */
	cycles = min(residual_burst, poll_each_burst);

	SDT_PROBE3(device_polling,,, poll_cycles, 0, residual_burst, cycles);

	max_rx = 0;
	for (i = 0 ; i < poll_handlers ; i++) {
		SDT_PROBE3(device_polling,,, before_poller, 0,
		    pr[i].handler, pr[i].ifp);

		rxcount = pr[i].handler(pr[i].ifp, arg, cycles);
		max_rx = max(rxcount, max_rx);

		SDT_PROBE5(device_polling,,, after_poller, 0,
		    pr[i].handler, pr[i].ifp, rxcount, cycles);
	}
	poll_tick_packets += max_rx;

	/*
	 * Check whether any poller handled enough packets for it to be
	 * worthwhile to do another iteration of polling.  netisr_pollmore()
	 * will reschedule another iteration if !polling_done.
	 */
	polling_done = max_rx < (cycles / 2 + 1);
	poll_phase = POLLER_POLL_DONE;

	SDT_PROBE5(device_polling,,, pollers_done, 0, max_rx, cycles,
	    polling_done, poll_ticks_at_start);

	mtx_unlock(&poll_mtx);
}

/*
 * Try to register routine for polling. Returns 0 if successful
 * (and polling should be enabled), error code otherwise.
 * A device is not supposed to register itself multiple times.
 *
 * This is called from within the *_ioctl() functions.
 */
int
ether_poll_register(poll_handler_t *h, if_t ifp)
{
	int i;

	KASSERT(h != NULL, ("%s: handler is NULL", __func__));
	KASSERT(ifp != NULL, ("%s: ifp is NULL", __func__));

	mtx_lock(&poll_mtx);
	if (poll_handlers >= POLL_LIST_LEN) {
		/*
		 * List full, cannot register more entries.
		 * This should never happen; if it does, it is probably a
		 * broken driver trying to register multiple times. Checking
		 * this at runtime is expensive, and won't solve the problem
		 * anyways, so just report a few times and then give up.
		 */
		static int verbose = 10 ;
		if (verbose >0) {
			log(LOG_ERR, "poll handlers list full, "
			    "maybe a broken driver ?\n");
			verbose--;
		}
		mtx_unlock(&poll_mtx);
		return (ENOMEM); /* no polling for you */
	}

	for (i = 0 ; i < poll_handlers ; i++)
		if (pr[i].ifp == ifp && pr[i].handler != NULL) {
			mtx_unlock(&poll_mtx);
			log(LOG_DEBUG, "ether_poll_register: %s: handler"
			    " already registered\n", ifp->if_xname);
			return (EEXIST);
		}

	pr[poll_handlers].handler = h;
	pr[poll_handlers].ifp = ifp;
	poll_handlers++;
	mtx_unlock(&poll_mtx);
	if (idlepoll_sleeping)
		wakeup(&idlepoll_sleeping);
	return (0);
}

/*
 * Remove interface from the polling list. Called from *_ioctl(), too.
 */
int
ether_poll_deregister(if_t ifp)
{
	int i;

	KASSERT(ifp != NULL, ("%s: ifp is NULL", __func__));

	mtx_lock(&poll_mtx);

	for (i = 0 ; i < poll_handlers ; i++)
		if (pr[i].ifp == ifp) /* found it */
			break;
	if (i == poll_handlers) {
		log(LOG_DEBUG, "ether_poll_deregister: %s: not found!\n",
		    ifp->if_xname);
		mtx_unlock(&poll_mtx);
		return (ENOENT);
	}
	poll_handlers--;
	if (i < poll_handlers) { /* Last entry replaces this one. */
		pr[i].handler = pr[poll_handlers].handler;
		pr[i].ifp = pr[poll_handlers].ifp;
	}
	mtx_unlock(&poll_mtx);
	return (0);
}

static void
poll_idle(void)
{
	struct thread *td = curthread;
	struct rtprio rtp;

	rtp.prio = RTP_PRIO_MAX;	/* lowest priority */
	rtp.type = RTP_PRIO_IDLE;
	PROC_SLOCK(td->td_proc);
	rtp_to_pri(&rtp, td);
	PROC_SUNLOCK(td->td_proc);

	for (;;) {
		if (poll_in_idle_loop && poll_handlers > 0) {
			idlepoll_sleeping = 0;
			ether_poll(poll_each_burst);
			thread_lock(td);
			mi_switch(SW_VOL, NULL);
			thread_unlock(td);
		} else {
			idlepoll_sleeping = 1;
			tsleep(&idlepoll_sleeping, 0, "pollid", hz * 3);
		}
	}
}

static struct proc *idlepoll;
static struct kproc_desc idlepoll_kp = {
	 "idlepoll",
	 poll_idle,
	 &idlepoll
};
SYSINIT(idlepoll, SI_SUB_KTHREAD_VM, SI_ORDER_ANY, kproc_start,
    &idlepoll_kp);

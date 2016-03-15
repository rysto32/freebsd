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
#include <sys/limits.h>
#include <sys/malloc.h>
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

#include <machine/stdarg.h>

void hardclock_device_poll(void);	/* hook from hardclock		*/

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
 *      0 - Poller instance.
 */
SDT_PROBE_DEFINE1(device_polling,,, netisr_sched, "struct poller_instance *");

/*
 * Probe called before netisr_pollmore decides whether to schedule another
 * iteration of polling.
 *
 * Arguments:
 *       0 - Poller instance.
 *       1 - Current time in us.
 *       2 - Current value of ticks
 */
SDT_PROBE_DEFINE3(device_polling,,, pollmore, "struct poller_instance *", "int", "int");

/*
 * Probe called when netisr_pollmore is going to schedule another iteration.
 *
 * Arguments:
 *       0 - Poller instance.
 */
SDT_PROBE_DEFINE1(device_polling,,, reschedule, "struct poller_instance *");

/*
 * Probe called when netisr_pollmore is not going to schedule another iteration
 * and the poller is going to "sleep" until the next tick.
 *
 * Arguments:
 *       0 - Poller instance.
 *       1 - Current time in us.
 *       2 - Current value of ticks
 */
SDT_PROBE_DEFINE3(device_polling,,, sleep, "struct poller_instance *", "int", "int");

/*
 * Probe called on the first polling iteration of a tick.
 *
 * Arguments:
 *       0 - Poller instance.
 */
SDT_PROBE_DEFINE1(device_polling,,, first_poll, "struct poller_instance *");

/*
 * Probe called after the poller has calculated how many packets to handle in
 * this iteration.
 *
 * Arguments:
 *       0 - Poller instance.
 *       1 - Number of packets that we have time to handle.
 *       2 - Number of packets that we are going to handle in this iteration.
 */
SDT_PROBE_DEFINE3(device_polling,,, poll_cycles, "struct poller_instance *", "int", "int");

/*
 * Probe called a polling handler is called.
 *
 * Arguments:
 *       0 - Poller instance.
 *       1 - Pointer to the object being polled.
 */
SDT_PROBE_DEFINE2(device_polling,,, before_poller, "struct poller_instance *",
    "struct dev_poll_entry *");

/*
 * Probe called after a polling handler returns.
 *
 * Arguments:
 *       0 - Poller instance.
 *       1 - Pointer to the object being polled.
 *       2 - The number of packets handled by the ifnet in this iteration.
 *       3 - The maximum number of packets we told the ifnet to handle.
 */
SDT_PROBE_DEFINE4(device_polling,,, after_poller, "struct poller_instance *",
    "struct dev_poll_entry *", "int", "int");

/*
 * Probe called after netisr_poll has called into every poller.
 *
 * Arguments:
 *       0 - Poller instance.
 *       1 - Largest number of packets handled by a handler in this iteration.
 *       2 - Maximum number of packets we told handlers to handle.
 */
SDT_PROBE_DEFINE3(device_polling,,, pollers_done, "struct poller_instance *",
    "int", "int");

#define POLLEE_ENTRY_NAME_LEN 64

struct poller_instance;

struct dev_poll_entry {
	struct poller_instance *instance;
	dev_poll_handler_t *handler;
	void *arg;
	char name[POLLEE_ENTRY_NAME_LEN];
	TAILQ_ENTRY(dev_poll_entry) next;
};

struct ether_pollee_entry
{
	struct dev_poll_entry pollee;
	poll_handler_t *handler;
	struct ifnet *ifp;
};

struct poller_instance
{
	struct mtx_padalign poll_mtx;

	int index;
	uint32_t lost_polls;
	uint32_t pending_polls;
	uint32_t poll_handlers; /* next free entry in pr[]. */
	enum poller_phase poll_phase;
	uint32_t suspect;
	uint32_t stalled;
	u_int poll_min_reschedule;
	int poll_ns_per_count;

	int poll_start_usec;
	int poll_end_usec;
	int poll_done_usec;
	int polling_done;
	int poll_ticks_at_start;
	int poll_tick_packets;
	int last_hardclock;
	uint32_t reg_frac_count;

	TAILQ_HEAD(, dev_poll_entry) pollee_list;
};

#define POLLER_LOCK(p) mtx_lock(&(p)->poll_mtx)
#define POLLER_UNLOCK(p) mtx_unlock(&(p)->poll_mtx)

static struct poller_instance *instances;
static u_int num_instances;

/*
 * This mutex prevents multiple pollee registrations from happening
 * concurrently, to ensure that we can accurately spread pollees across poller
 * intances uniformly.
 */
static struct mtx poll_register_mtx;

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

	// XXX locking
	poll_each_burst = val;

	return (0);
}
SYSCTL_PROC(_kern_polling, OID_AUTO, each_burst, CTLTYPE_UINT | CTLFLAG_RW,
	NULL, 0, poll_each_burst_sysctl, "I",
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

	// XXX locking?
	user_frac = val;

	return (0);
}
SYSCTL_PROC(_kern_polling, OID_AUTO, user_frac, CTLTYPE_UINT | CTLFLAG_RW,
	NULL, 0, user_frac_sysctl, "I",
	"Desired user fraction of cpu time");

static uint32_t reg_frac = 20 ;
static int reg_frac_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct poller_instance *poller;
	uint32_t val = reg_frac;
	u_int i;
	int error;

	poller = arg1;

	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error || !req->newptr )
		return (error);
	if (val < 1 || val > hz)
		return (EINVAL);

	// XXX locking?
	reg_frac = val;

	for (i = 0; i < num_instances; i++) {
		poller = &instances[i];
		POLLER_LOCK(poller);
		if (poller->reg_frac_count >= reg_frac)
			poller->reg_frac_count = 0;
		POLLER_UNLOCK(poller);
	}

	return (0);
}
SYSCTL_PROC(_kern_polling, OID_AUTO, reg_frac, CTLTYPE_UINT | CTLFLAG_RW,
	NULL, 0, reg_frac_sysctl, "I",
	"Every this many cycles check registers");

static uint32_t short_ticks;
SYSCTL_UINT(_kern_polling, OID_AUTO, short_ticks, CTLFLAG_RD,
	&short_ticks, 0, "Hardclock ticks shorter than they should be");

static uint32_t idlepoll_sleeping; /* idlepoll is sleeping */
SYSCTL_UINT(_kern_polling, OID_AUTO, idlepoll_sleeping, CTLFLAG_RD,
	&idlepoll_sleeping, 0, "idlepoll is sleeping");

static int poll_min_reschedule = 2;
SYSCTL_INT(_kern_polling, OID_AUTO, min_reschedule, CTLFLAG_RW,
    &poll_min_reschedule, 0,
    "minimum number of packets to handle in a polling iteration");

static struct sysctl_ctx_list poller_ctx;

static MALLOC_DEFINE(M_DEVICE_POLL, "device_polling", "Network polling subsystem");

static dev_poll_handler_t ether_poll_handler;

static void
poll_shutdown(void *arg, int howto)
{

	poll_shutting_down = 1;
	sysctl_ctx_free(&poller_ctx);
}

static void
init_poller_sysctls(struct poller_instance *poller, int index)
{
	struct sysctl_oid *oid;
	char name [32];

	snprintf(name, sizeof(name), "%d", index);
	oid = SYSCTL_ADD_NODE(&poller_ctx, SYSCTL_STATIC_CHILDREN(_kern_polling),
	    OID_AUTO, name, CTLFLAG_RD, NULL, "Poller instance stats");

	SYSCTL_ADD_UINT(&poller_ctx, SYSCTL_CHILDREN(oid), OID_AUTO,
	    "lost_polls", CTLFLAG_RD, &poller->lost_polls, 0,
	    "How many times we would have lost a poll tick");

	SYSCTL_ADD_UINT(&poller_ctx, SYSCTL_CHILDREN(oid), OID_AUTO,
	     "pending_polls", CTLFLAG_RD, &poller->pending_polls, 0,
	     "Do we need to poll again");

	SYSCTL_ADD_UINT(&poller_ctx, SYSCTL_CHILDREN(oid), OID_AUTO, "handlers",
	    CTLFLAG_RD, &poller->poll_handlers, 0,
	    "Number of registered poll handlers");

	SYSCTL_ADD_INT(&poller_ctx, SYSCTL_CHILDREN(oid), OID_AUTO, "phase",
	    CTLFLAG_RD, (int*)&poller->poll_phase, 0, "Polling phase");

	SYSCTL_ADD_UINT(&poller_ctx, SYSCTL_CHILDREN(oid), OID_AUTO, "suspect",
	    CTLFLAG_RD, &poller->suspect, 0, "suspect event");

	SYSCTL_ADD_UINT(&poller_ctx, SYSCTL_CHILDREN(oid), OID_AUTO, "stalled",
	    CTLFLAG_RD, &poller->stalled, 0, "potential stalls");

	SYSCTL_ADD_INT(&poller_ctx, SYSCTL_CHILDREN(oid), OID_AUTO, "ns_per_packet",
	    CTLFLAG_RD, &poller->poll_ns_per_count, 0,
	    "Average number of nanoseconds required to handle 1 packet");
}

static void
init_poller(struct poller_instance *poller, int index)
{

	mtx_init(&poller->poll_mtx, "polling", NULL, MTX_DEF);
	poller->index = index;
	TAILQ_INIT(&poller->pollee_list);

	init_poller_sysctls(poller, index);
}

static void
init_device_poll(void)
{
	u_int i;

	mtx_init(&poll_register_mtx, "polling register", NULL, MTX_DEF);
	sysctl_ctx_init(&poller_ctx);

	num_instances = netisr_get_cpucount();
	instances = malloc(sizeof(*instances) * num_instances, M_DEVICE_POLL,
	    M_WAITOK | M_ZERO);
	for (i = 0; i < num_instances; i++)
		init_poller(&instances[i], i);
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
	struct poller_instance *poller;
	static int prev_usec;
	struct timeval t;
	u_int i, num_netisr;
	int usec;
	int delta;
	int pending;

	if (poll_shutting_down)
		return;

	microuptime(&t);
	usec = tv_to_usec(t);
	delta = usec - prev_usec;
	if (delta * hz < 500000)
		short_ticks++;
	else
		prev_usec = usec;

	num_netisr = num_instances;
	for (i = 0; i < num_netisr; i++) {
		poller = &instances[i];
		pending = atomic_fetchadd_int(&poller->pending_polls, 1);
		if (pending > 100) {
			poller->stalled++;
			poller->pending_polls = 0;
		} else if (pending > 0)
			poller->lost_polls++;

		if (poller->poll_phase != POLLER_SLEEPING)
			poller->suspect++;

		poller->last_hardclock = usec;
		SDT_PROBE1(device_polling,,, netisr_sched, poller);
		netisr_sched_poll(poller->index);
	}
}

// XXX axe?
#if 0
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
#endif

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

void
netisr_pollmore(u_int id)
{
	struct poller_instance *poller;
	struct timeval now;
	int usec;
	int ticks_now;

	poller = &instances[id];
	if (poller->poll_handlers == 0)
		return;

	POLLER_LOCK(poller);
	poller->poll_phase = POLLER_POLLMORE;

	/* here we can account time spent in netisr's in this tick */
	microuptime(&now);
	usec = tv_to_usec(now);

	ticks_now = ticks;

	SDT_PROBE3(device_polling,,, pollmore, poller, usec, ticks_now);

	/*
	 * Schedule another poll if we have enough time to do at least
	 * poll_min_reschedule packets, if we aren't done polling for lack of
	 * work to do and if we haven't crossed a tick boundary(if we've crossed
	 * a tick boundary there is a pending wakeup waiting from hardclock
	 * which kick off this tick's round of polling).
	 */
	if ((poller->poll_end_usec - usec) * 1000 >
	    poller->poll_ns_per_count * poll_min_reschedule &&
	    !poller->polling_done && poller->poll_ticks_at_start == ticks_now) {
		poller->polling_done = 0;
		netisr_sched_poll(poller->index);

		SDT_PROBE1(device_polling,,, reschedule, poller);
	} else {
		SDT_PROBE3(device_polling,,, sleep, poller, usec, ticks_now);

		poller->polling_done = 0;
		poller->pending_polls = 0;
		poller->poll_phase = POLLER_SLEEPING;
		poller->poll_done_usec = usec;
	}
	POLLER_UNLOCK(poller);
}

/*
 * netisr_poll is typically scheduled once per tick.
 */
void
netisr_poll(u_int id)
{
	struct timeval now_t;
	struct poller_instance *poller;
	struct dev_poll_entry *entry;
	int cycles;
	enum poll_cmd cmd;
	int rxcount, remaining_usec, max_rx, deltausec;
	int newnsper, residual_burst, now_usec;

	poller = &instances[id];
	if (poller->poll_handlers == 0)
		return;

	cmd = POLL_ONLY;

	POLLER_LOCK(poller);
	microuptime(&now_t);
	now_usec = tv_to_usec(now_t);
	if (poller->poll_phase == POLLER_SLEEPING) { /* first call in this tick */
		atomic_subtract_int(&poller->pending_polls, 1);

		if (poller->poll_tick_packets > 0) {
			deltausec = poller->poll_done_usec - poller->poll_start_usec;
			newnsper = deltausec * 1000 / poller->poll_tick_packets;

			poller->poll_ns_per_count = (newnsper * POLL_NS_AVG_NEW
			    + poller->poll_ns_per_count * POLL_NS_AVG_OLD)
			    / POLL_NS_AVG_DEN;
			poller->poll_tick_packets = 0;
		}
		poller->poll_start_usec = now_usec;
		poller->poll_ticks_at_start = ticks;
		if (++poller->reg_frac_count == reg_frac) {
			cmd = POLL_AND_CHECK_STATUS;
			poller->reg_frac_count = 0;
		}

		/*
		 * 10000 = 1000000 us/s divided by 100 to convert user_frac to a
		 * percentage.
		 */
		poller->poll_end_usec = poller->last_hardclock +
		    (100 - user_frac) * 10000 / hz;

		SDT_PROBE1(device_polling,,, first_poll, poller);
	}
	poller->poll_phase = POLLER_POLL;
	remaining_usec = poller->poll_end_usec - now_usec;

	/*
	 * If poll_ns_per_count is invalid replace it with an arbitrary,
	 * conservative value.
	 */
	if (poller->poll_ns_per_count <= 0)
		poller->poll_ns_per_count = 50000;

	/* Calculate how many more packets we have time to do in this tick. */
	residual_burst = remaining_usec * 1000 /poller-> poll_ns_per_count;
	if (residual_burst <= 0)
		residual_burst = 1;
	/*
	 * Don't do more than poll_each_burst packets in this iteration.  If
	 * there's a lot of packets waiting we'll get them in the next
	 * iteration.
	 */
	cycles = min(residual_burst, poll_each_burst);

	SDT_PROBE3(device_polling,,, poll_cycles, poller, residual_burst, cycles);

	max_rx = 0;
	TAILQ_FOREACH(entry, &poller->pollee_list, next) {
		SDT_PROBE2(device_polling,,, before_poller, poller, entry);

		rxcount = entry->handler(entry->arg, cmd, cycles);
		max_rx = max(rxcount, max_rx);

		SDT_PROBE4(device_polling,,, after_poller, poller, entry, rxcount,
		    cycles);
	}
	poller->poll_tick_packets += max_rx;

	/*
	 * Check whether any poller handled enough packets for it to be
	 * worthwhile to do another iteration of polling.  netisr_pollmore()
	 * will reschedule another iteration if !polling_done.
	 */
	poller->polling_done = max_rx < (cycles / 2 + 1);
	poller->poll_phase = POLLER_POLL_DONE;

	SDT_PROBE3(device_polling,,, pollers_done, poller, max_rx, cycles);

	POLLER_UNLOCK(poller);
}

static struct poller_instance *
get_least_loaded(void)
{
	struct poller_instance *poller;
	u_int i;

	mtx_assert(&poll_register_mtx, MA_OWNED);

	poller = &instances[0];
	for (i = 1; i < num_instances; i++) {
		if (instances[i].poll_handlers < poller->poll_handlers)
			poller = &instances[i];
	}

	return (poller);
}

/*
 * Try to register routine for polling. Returns 0 if successful
 * (and polling should be enabled), error code otherwise.
 * A device is not supposed to register itself multiple times.
 *
 * This is called from within the *_ioctl() functions.
 */
static int
dev_poll_register_locked(dev_poll_handler_t * h, void *arg, u_int index,
    struct dev_poll_entry *entry, const char *format, __va_list ap)
{
	struct poller_instance *poller;

	if (entry->instance != NULL) {
		log(LOG_DEBUG, "dev_poll_register: %s: handler"
			" already registered\n", entry->name);
		return (EEXIST);
	}

	if (index == DEV_POLL_ANY)
		poller = get_least_loaded();
	else if(index < num_instances)
		poller = &instances[index];
	else
		return (ENOENT);

	entry->handler = h;
	entry->arg = arg;
	entry->instance = poller;

	vsnprintf(entry->name, sizeof(entry->name), format, ap);

	POLLER_LOCK(poller);
	TAILQ_INSERT_TAIL(&poller->pollee_list, entry, next);
	poller->poll_handlers++;
	POLLER_UNLOCK(poller);

	if (idlepoll_sleeping)
		wakeup(&idlepoll_sleeping);

	return (0);
}

static int
dev_poll_register_locked_va(dev_poll_handler_t * h, void *arg, u_int index,
    struct dev_poll_entry *entry, const char *format, ...)
{
	__va_list ap;
	int error;

	va_start(ap, format);
	error = dev_poll_register_locked(h, arg, index, entry, format, ap);
	va_end(ap);
	return (error);
}

int
dev_poll_register(dev_poll_handler_t * h, void *arg, u_int index,
    struct dev_poll_entry *entry, const char *format, ...)
{
	int error;
	__va_list ap;

	mtx_lock(&poll_register_mtx);
	va_start(ap, format);
	error = dev_poll_register_locked(h, arg, index, entry, format, ap);
	va_end(ap);
	mtx_unlock(&poll_register_mtx);

	return (error);
}


/*
 * Remove interface from the polling list. Called from *_ioctl(), too.
 */
int
dev_poll_deregister(struct dev_poll_entry *entry)
{
	struct poller_instance *poller;
	int error;

	mtx_lock(&poll_register_mtx);

	if (entry->instance == NULL) {
		log(LOG_DEBUG, "ether_poll_deregister: '%s': not found!\n",
		    entry->name);
		error = ENOENT;
		goto out;
	}

	poller = entry->instance;

	POLLER_LOCK(poller);
	entry->instance = NULL;
	TAILQ_REMOVE(&poller->pollee_list, entry, next);
	poller->poll_handlers--;
	POLLER_UNLOCK(poller);

	error = 0;
out:
	mtx_unlock(&poll_register_mtx);

	return (error);
}

int
ether_poll_register(poll_handler_t *h, if_t ifp)
{
	struct ether_pollee_entry *entry;
	int error;

	mtx_lock(&poll_register_mtx);

	entry = ifp->pollee;

	if (entry->pollee.instance != NULL) {
		log(LOG_DEBUG, "ether_poll_register: %s: handler"
			" already registered\n", entry->pollee.name);
		error = EEXIST;
		goto out;
	}

	entry->handler = h;
	entry->ifp = ifp;

	error = dev_poll_register_locked_va(ether_poll_handler, entry,
	    DEV_POLL_ANY, &entry->pollee, "%s", ifp->if_xname);

out:
	mtx_unlock(&poll_register_mtx);

	return (error);
}

int
ether_poll_deregister(if_t ifp)
{
	struct ether_pollee_entry *entry;

	entry = ifp->pollee;
	return (dev_poll_deregister(&entry->pollee));
}

struct ether_pollee_entry *
ether_pollee_entry_alloc(void)
{

	return (malloc(sizeof(struct ether_pollee_entry), M_DEVICE_POLL,
	    M_WAITOK | M_ZERO));
}

void
ether_pollee_entry_free(struct ether_pollee_entry *entry)
{

	free(entry, M_DEVICE_POLL);
}

struct dev_poll_entry *
dev_poll_entry_alloc(int how)
{

	KASSERT(how == M_WAITOK || how == M_NOWAIT,
	    ("%s: how=%x is invalid", __func__, how));

	return (malloc(sizeof(struct dev_poll_entry), M_DEVICE_POLL,
	    how | M_ZERO));
}

void
dev_poll_entry_free(struct dev_poll_entry *entry)
{

	free(entry, M_DEVICE_POLL);
}

static int
ether_poll_handler(void *arg, enum poll_cmd cmd, int count)
{
	struct ether_pollee_entry *entry;

	entry = arg;
	return (entry->handler(entry->ifp, cmd, count));
}

// XXX axe?
#if 0
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
#endif

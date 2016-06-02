/*-
 * Copyright (c) 1982, 1986, 1993, 1995
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)tcp_seq.h	8.3 (Berkeley) 6/21/95
 * $FreeBSD$
 */

#ifndef _NETINET_TCP_SEQ_H_
#define _NETINET_TCP_SEQ_H_
/*
 * TCP sequence numbers are 32 bit integers operated
 * on with modular arithmetic.  These macros can be
 * used to compare such integers.
 */
#define	SEQ_LT(a,b)	((int)((a)-(b)) < 0)
#define	SEQ_LEQ(a,b)	((int)((a)-(b)) <= 0)
#define	SEQ_GT(a,b)	((int)((a)-(b)) > 0)
#define	SEQ_GEQ(a,b)	((int)((a)-(b)) >= 0)

#define	SEQ_MIN(a, b)	((SEQ_LT(a, b)) ? (a) : (b))
#define	SEQ_MAX(a, b)	((SEQ_GT(a, b)) ? (a) : (b))

/* for modulo comparisons of timestamps */
#define TSTMP_LT(a,b)	((int)((a)-(b)) < 0)
#define TSTMP_GT(a,b)	((int)((a)-(b)) > 0)
#define TSTMP_GEQ(a,b)	((int)((a)-(b)) >= 0)

/*
 * Macros to initialize tcp sequence numbers for
 * send and receive from initial send and receive
 * sequence numbers.
 */
#define	tcp_rcvseqinit(tp) \
	(tp)->rcv_adv = (tp)->rcv_nxt = (tp)->irs + 1

#define	tcp_sendseqinit(tp) \
	(tp)->snd_una = (tp)->snd_nxt = (tp)->snd_max = (tp)->snd_up = \
	    (tp)->snd_recover = (tp)->iss

#ifdef _KERNEL

/*
 * RFC 7323
 * Section 5.4. Timestamp Clock
 *
 *  (b)  The timestamp clock must not be "too fast".
 *
 *      The recycling time of the timestamp clock MUST be greater than
 *      MSL seconds.  Since the clock (timestamp) is 32 bits and the
 *      worst-case MSL is 255 seconds, the maximum acceptable clock
 *      frequency is one tick every 59 ns.
 */

/*
 * The timestamp granularity (in seconds) can be calculated as follows:
 * (1 << SBT_MINTS_SHIFT)/(SBT_1S).
 *
 * The minimum permissible timestamp is 59ns. However, to reduce calculation
 * overhead we use 256 - (8 bit shift).
 *  - (1<<32)/(1000000000/59) == 253
 *  - (1<<32)/(1000000000/60) == 257
 *
 *
 * Note that MSL should be a function of RTT. Although 60ns is more than sufficient resolution for
 * the time being a 255s MSL on data center network with a sub-millisecond RTT doesn't make a whole
 * lot of senese. In the future the MSL should be determined dynamically or at the very least con-
 * figurable per subnet. Nonetheless, fixing the timestamp clock at a rate corresponding to a 256s
 * MSL gives us what we need for now while otherwise remaining as RFC compliant as possible.
 *
 * Instead of usng a shift that would lead to the minimum possibly granularity
 * we instead use a shift of 15, which leads to a granularity of 7.56us. We
 * choose this over the minimum 59ns because we have observed problems with
 * certain middleboxes erroneously dropping packets from a TCP stream once the
 * TS rolls over. Setting the granularity higher makes it so that it will
 * usually take several hours for the TS to wrap around, instead of several
 * minutes with the minimum granularity.
 *
 */

#define SBT_MINTS_SHIFT 8
#define	MIN_TS_STEP 2
#define TS_1S (SBT_1S >> SBT_MINTS_SHIFT)
#define SBT_MINTS (1 << SBT_MINTS_SHIFT)
#define SBT_MINRTT SBT_MINTS

/*
 * Clock macros for RFC 1323 timestamps.
 */
#define	TCP_TS_TO_SBT(_t)	((_t) << SBT_MINTS_SHIFT)
#define	TCP_SBT_TO_TS(_t)	((_t) >> SBT_MINTS_SHIFT)


#define MAX_TS_STEP	((1<<30))

/*
 * RFC defined MSL: 255s (+ 2s rounding slop)
 */
#define TCP_PAWS_IDLE	(SBT_MINTS*SBT_1S/2)

extern sbintime_t (*cpu_ts_getsbintime)(void);
#define tcp_ts_getsbintime() (cpu_ts_getsbintime)()
#endif /* _KERNEL */

#endif /* _NETINET_TCP_SEQ_H_ */

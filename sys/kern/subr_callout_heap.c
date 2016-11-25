/*-
 * Copyright (c) 2016 Dell Inc.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>

#include <sys/_callout.h>
#include <sys/callout_int.h>

static __inline bool
callout_heap_next_left(struct callout_heap *cc)
{

	return ((cc->cc_numcallouts & 1) == 0);
}

static __inline bool
is_left_child(struct callout *parent, struct callout *child)
{

	return (parent->c_links.heap.left == child);
}

static struct callout*
callout_heap_next_parent(struct callout_heap *cc)
{
	struct callout *parent;
	int mask;

	mask = 1 << (fls(cc->cc_numcallouts) - 1);
	mask >>= 1;
	parent = cc->cc_heap;
	while (mask > 1) {
		if (cc->cc_numcallouts & mask)
			parent = parent->c_links.heap.right;
		else
			parent = parent->c_links.heap.left;
		mask >>= 1;
	}

	return (parent);
}

static bool
callout_heap_sift_up(struct callout_heap *cc, struct callout *c)
{
	struct callout *parent, *sibling, *grand;

	parent = c->c_links.heap.parent;
	while (parent != NULL) {
		if (parent->c_time <= c->c_time)
			return (false);

		grand = parent->c_links.heap.parent;
		if (is_left_child(parent, c)) {
			sibling = parent->c_links.heap.right;

			parent->c_links.heap.left = c->c_links.heap.left;
			if (parent->c_links.heap.left != NULL)
				c->c_links.heap.left->c_links.heap.parent = parent;

			parent->c_links.heap.right = c->c_links.heap.right;
			if (c->c_links.heap.right != NULL)
				c->c_links.heap.right->c_links.heap.parent = parent;

			c->c_links.heap.right = sibling;
			if (sibling != NULL)
				sibling->c_links.heap.parent = c;

			c->c_links.heap.left = parent;
			parent->c_links.heap.parent = c;
		} else {
			sibling = parent->c_links.heap.left;

			parent->c_links.heap.left = c->c_links.heap.left;
			if (parent->c_links.heap.left != NULL)
				c->c_links.heap.left->c_links.heap.parent = parent;

			parent->c_links.heap.right = c->c_links.heap.right;
			if (c->c_links.heap.right != NULL)
				c->c_links.heap.right->c_links.heap.parent = parent;

			c->c_links.heap.left = sibling;
			if (sibling != NULL)
				sibling->c_links.heap.parent = c;

			c->c_links.heap.right = parent;
			parent->c_links.heap.parent = c;
		}

		c->c_links.heap.parent = grand;
		if (grand != NULL) {
			if (is_left_child(grand, parent))
				grand->c_links.heap.left = c;
			else
				grand->c_links.heap.right = c;
		}


		parent = c->c_links.heap.parent;
	}

	/*
	 * Callout has made its way to the top of the heap.  Update the pointer
	 * to the top of the heap.
	 */
	cc->cc_heap = c;

	return (true);
}

static void
callout_heap_sift_down(struct callout_heap *cc, struct callout *p)
{
	struct callout *left, *right, *grand;

	left = p->c_links.heap.left;
	while (left != NULL) {
		right = p->c_links.heap.right;
		if (right == NULL || left->c_time < right->c_time) {
			if (left->c_time >= p->c_time)
				return;

			grand = p->c_links.heap.parent;
			if (grand != NULL) {
				if (is_left_child(grand, p))
					grand->c_links.heap.left = left;
				else
					grand->c_links.heap.right = left;
			}
			left->c_links.heap.parent = grand;

			p->c_links.heap.right = left->c_links.heap.right;
			if (p->c_links.heap.right != NULL)
				p->c_links.heap.right->c_links.heap.parent = p;

			p->c_links.heap.left = left->c_links.heap.left;
			if (p->c_links.heap.left != NULL)
				p->c_links.heap.left->c_links.heap.parent = p;

			left->c_links.heap.left = p;
			p->c_links.heap.parent = left;

			left->c_links.heap.right = right;
			if (right != NULL)
				right->c_links.heap.parent = left;

			if (cc->cc_heap == p)
				cc->cc_heap = left;

		} else {
			if (right->c_time >= p->c_time)
				return;

			grand = p->c_links.heap.parent;
			if (grand != NULL) {
				if (is_left_child(grand, p))
					grand->c_links.heap.left = right;
				else
					grand->c_links.heap.right = right;
			}
			right->c_links.heap.parent = grand;

			p->c_links.heap.left = right->c_links.heap.left;
			if (p->c_links.heap.left != NULL)
				p->c_links.heap.left->c_links.heap.parent = p;

			p->c_links.heap.right = right->c_links.heap.right;
			if (p->c_links.heap.right != NULL)
				p->c_links.heap.right->c_links.heap.parent = p;

			right->c_links.heap.right = p;
			p->c_links.heap.parent = right;

			right->c_links.heap.left = left;
			left->c_links.heap.parent = right;

			if (cc->cc_heap == p)
				cc->cc_heap = right;
		}
		left = p->c_links.heap.left;
	}
}

void
callout_heap_init(struct callout_heap *cc)
{

	cc->cc_heap = NULL;
	cc->cc_numcallouts = 0;
}

bool
callout_heap_insert(struct callout_heap *cc, struct callout *c)
{
	struct callout *parent;

	if (cc->cc_heap == NULL) {
		KASSERT(cc->cc_numcallouts == 0,
		    ("numcallouts %d out of sync", cc->cc_numcallouts));
		cc->cc_numcallouts = 1;
		cc->cc_heap = c;

		c->c_links.heap.parent = NULL;
		c->c_links.heap.left = NULL;
		c->c_links.heap.right = NULL;
		return (true);
	}

	cc->cc_numcallouts++;
	parent = callout_heap_next_parent(cc);

	if (callout_heap_next_left(cc)) {
		KASSERT(parent->c_links.heap.left == NULL,
		    ("heap corrupt: inserted into used position"));
		KASSERT(parent->c_links.heap.right == NULL,
		    ("heap corrupt: right occupied but left empty"));

		parent->c_links.heap.left = c;
	} else {
		KASSERT(parent->c_links.heap.left != NULL,
		    ("heap corrupt: inserting into right with left empty"));
		KASSERT(parent->c_links.heap.right == NULL,
		    ("heap corrupt: inserted into used position"));

		parent->c_links.heap.right = c;
	}
	c->c_links.heap.parent = parent;
	c->c_links.heap.left = NULL;
	c->c_links.heap.right = NULL;

	return (callout_heap_sift_up(cc, c));
}

bool
callout_heap_remove(struct callout_heap *cc, struct callout *rm)
{
	struct callout *parent, *c;
	bool removed_next;

	if (cc->cc_numcallouts == 1) {
		KASSERT(rm == cc->cc_heap, ("Removing callout not in heap"));

		cc->cc_numcallouts = 0;
		cc->cc_heap = NULL;

		return (true);
	}

	parent = callout_heap_next_parent(cc);

	if (callout_heap_next_left(cc)) {
		c = parent->c_links.heap.left;
		parent->c_links.heap.left = NULL;
	} else {
		c = parent->c_links.heap.right;
		parent->c_links.heap.right = NULL;
	}
	cc->cc_numcallouts--;

	if (c == rm)
		return (false);

	KASSERT(c != NULL, ("heap corrupt: NULL callout"));

	c->c_links.heap.parent = rm->c_links.heap.parent;
	c->c_links.heap.left = rm->c_links.heap.left;
	if (c->c_links.heap.left != NULL)
		c->c_links.heap.left->c_links.heap.parent = c;
	c->c_links.heap.right = rm->c_links.heap.right;
	if (c->c_links.heap.right != NULL)
		c->c_links.heap.right->c_links.heap.parent = c;
	if (c->c_links.heap.parent == NULL) {
		cc->cc_heap = c;
		removed_next = true;
	} else {
		removed_next = false;
		if (c->c_links.heap.parent->c_links.heap.left == rm)
			c->c_links.heap.parent->c_links.heap.left = c;
		else
			c->c_links.heap.parent->c_links.heap.right = c;
	}

	if (c->c_links.heap.parent != NULL &&
	    c->c_links.heap.parent->c_time > c->c_time)
		callout_heap_sift_up(cc, c);
	else
		callout_heap_sift_down(cc, c);

	return (removed_next);
}

#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"%Z%%M%	%I%	%E% SMI"

script()
{
	$dtrace -qs /dev/stdin <<EOF
	profile-1234hz
	/arg0 != 0/
	{
		@[sym(arg0)] = count();
	}

	tick-100ms
	/i++ == 50/
	{
		exit(0);
	}
EOF
}

spinny()
{
	while true; do
		/bin/date > /dev/null
	done
}

if [ $# != 1 ]; then
	echo expected one argument: '<'dtrace-path'>'
	exit 2
fi

dtrace=$1

spinny &
child=$!

#
# This is the same gutsy test as that found in the func() test; see that
# test for the rationale.
#
script | tee /dev/fd/2 | grep mtx_lock > /dev/null
status=$?

kill $child
exit $status

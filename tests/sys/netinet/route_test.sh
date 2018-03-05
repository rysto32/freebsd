#
#  Copyright (c) 2014 Dell Inc
#  All rights reserved.
#
#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that the following conditions
#  are met:
#  1. Redistributions of source code must retain the above copyright
#     notice, this list of conditions, and the following disclaimer,
#     without modification.
#  2. Redistributions in binary form must reproduce at minimum a disclaimer
#     substantially similar to the "NO WARRANTY" disclaimer below
#     ("Disclaimer") and any redistribution must be conditioned upon
#     including a substantially similar Disclaimer requirement for further
#     binary redistribution.
#
#  NO WARRANTY
#  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
#  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
#  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
#  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
#  HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
#  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
#  OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
#  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
#  STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
#  IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
#  POSSIBILITY OF SUCH DAMAGES.

# All of the tests in this file requires the test-suite config variable "fibs"
# to be defined to a space-delimited list of FIBs that may be used for testing.

. $(atf_get_srcdir)/route.subr

atf_test_case ipv4_move_subnet_route cleanup
ipv4_move_subnet_route_head()
{
	atf_set "descr" "moving a subnet route to different ifa should be possible"
	atf_set "require.user" "root"
	atf_set "require.config" "fibs"
}

ipv4_move_subnet_route_body()
{
	# Configure the TAP interfaces to use a RFC5737 nonrouteable addresses
	# and a non-default fib
	SUBNET_PREFIX="192.0.2"
	SUBNET="${SUBNET_PREFIX}.0"
	ADDR0="${SUBNET_PREFIX}.1"
	ADDR1="${SUBNET_PREFIX}.2"

	MASK="24"

	# Check system configuration
	if [ 0 != `sysctl -n net.add_addr_allfibs` ]; then
		atf_skip "This test requires net.add_addr_allfibs=0"
	fi
	get_fibs 1

	get_epair
	setup_iface "$EPAIRA" "$FIB0" inet ${ADDR0} $MASK
	setup_iface "$EPAIRB" "$FIB0" inet ${ADDR1} $MASK

	setfib $FIB0 route change ${SUBNET}/${MASK} -ifp "$EPAIRB"
	ifconfig "$EPAIRA" inet ${ADDR0}/${MASK} fib "$FIB0" -alias
	atf_check -s exit:0 ifconfig "$EPAIRB" inet ${ADDR0}/$MASK alias fib "$FIB0"
}

ipv4_move_subnet_route_cleanup()
{
	cleanup_ifaces
}

atf_test_case ipv6_move_subnet_route cleanup
ipv6_move_subnet_route_head()
{
	atf_set "descr" "moving a subnet route to different ifa should be possible"
	atf_set "require.user" "root"
	atf_set "require.config" "fibs"
}

ipv6_move_subnet_route_body()
{
	# Configure the TAP interfaces to use a RFC5737 nonrouteable addresses
	# and a non-default fib
	SUBNET_PREFIX="2001:db8:"
	SUBNET="${SUBNET_PREFIX}:0"
	ADDR0="${SUBNET_PREFIX}:1"
	ADDR1="${SUBNET_PREFIX}:2"

	MASK="64"

	# Check system configuration
	if [ 0 != `sysctl -n net.add_addr_allfibs` ]; then
		atf_skip "This test requires net.add_addr_allfibs=0"
	fi
	get_fibs 1

	get_epair
	setup_iface "$EPAIRA" "$FIB0" inet6 ${ADDR0} $MASK
	setup_iface "$EPAIRB" "$FIB0" inet6 ${ADDR1} $MASK

	setfib $FIB0 route -6 change ${SUBNET}/${MASK} -ifp "$EPAIRB"
	ifconfig "$EPAIRA" inet6 ${ADDR0}/${MASK} fib "$FIB0" -alias
	atf_check -s exit:0 setfib 1 ifconfig "$EPAIRB" inet6 ${ADDR0}/$MASK fib "$FIB0" alias
}

ipv6_move_subnet_route_cleanup()
{
	cleanup_ifaces
}

atf_init_test_cases()
{
	atf_add_test_case ipv4_move_subnet_route
	atf_add_test_case ipv6_move_subnet_route
}

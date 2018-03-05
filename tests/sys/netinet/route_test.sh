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

# Test that a route can be modified with "route change $SUBNET/$MASK" without
# needing to also specify the gateway of the route.
atf_test_case ipv4_change_route_without_dest cleanup
ipv4_change_route_without_dest_head()
{
	atf_set "descr" "Test modifying a route without re-specifying its gateway"
	atf_set "require.user" "root"
	atf_set "require.config" "fibs"
}

ipv4_change_route_without_dest_body()
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

	setup_tap "$FIB0" inet ${ADDR0} $MASK
	ifconfig $TAP mtu 9000

	atf_check -s exit:0 -o ignore setfib $FIB0 route change \
	    ${SUBNET}/${MASK} -mtu 8723

	# Just searching for "8723" is awful, but the output of 'route get' is
	# not amenable to parsing
	atf_check -o match:8723 setfib $FIB0 route get $ADDR1
}

ipv4_change_route_without_dest_cleanup()
{
	cleanup_ifaces
}

# Test that a route can be modified with "route change $SUBNET/$MASK" without
# needing to also specify the gateway of the route.
atf_test_case ipv6_change_route_without_dest cleanup
ipv6_change_route_without_dest_head()
{
	atf_set "descr" "Test modifying a route without re-specifying its gateway"
	atf_set "require.user" "root"
	atf_set "require.config" "fibs"
}

ipv6_change_route_without_dest_body()
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

	setup_tap "$FIB0" inet6 ${ADDR0} $MASK
	ifconfig $TAP mtu 9000

	atf_check -s exit:0 -o ignore setfib $FIB0 route -6 change \
	    ${SUBNET}/${MASK} -mtu 8723

	# Just searching for "8723" is awful, but the output of 'route get' is
	# not amenable to parsing
	atf_check -o match:8723 setfib $FIB0 route -6 get $ADDR1
}

ipv6_change_route_without_dest_cleanup()
{
	cleanup_ifaces
}

atf_init_test_cases()
{
	atf_add_test_case ipv4_change_route_without_dest
	atf_add_test_case ipv6_change_route_without_dest
}

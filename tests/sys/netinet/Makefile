# $FreeBSD$

PACKAGE=	tests

TESTSDIR=	${TESTSBASE}/sys/netinet
BINDIR=		${TESTSDIR}

ATF_TESTS_SH=	\
		fibs_test \
		route_test \

${PACKAGE}FILES+=	\
		route.subr

PROGS=	udp_dontroute tcp_user_cookie

MAN=

WARNS?=	6

.include <bsd.test.mk>

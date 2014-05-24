# $FreeBSD$

TESTSDIR= ${TESTSBASE}/lib/libnv

ATF_TESTS_CXX=	\
	dnv_tests \
	nv_tests \

CFLAGS+= -I${.CURDIR:H}
LDADD+= -L${.OBJDIR:H} -lnv

WARNS?=	3

.include <bsd.test.mk>

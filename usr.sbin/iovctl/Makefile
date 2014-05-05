# $FreeBSD$

PROG=	iovctl
SRCS=	iovctl.c parse.c validate.c
DPADD=	${LIBNV} ${LIBUCL} ${LIBM}
LDADD=	-lnv -lucl -lm
USEPRIVATELIB=	ucl

CFLAGS+=-I${.CURDIR}/../../contrib/libucl/include

WARNS?=6

MAN=

.include <bsd.own.mk>
.include <bsd.prog.mk>


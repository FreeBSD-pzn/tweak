# Created by: Jille Timmermans (jille@quis.cx)
# $FreeBSD: head/editors/tweak/Makefile 462479 2018-02-21 09:02:56Z amdmi3 $

PORTNAME=	tweak
PORTVERSION=	3.02
CATEGORIES=	editors
MASTER_SITES=	https://github.com/FreeBSD-pzn/${PORTNAME}/raw/master/

MAINTAINER=	pzn.unixbsd@gmail.com
COMMENT=	Efficient hex editor

LICENSE=	MIT
LICENSE_FILE=	${WRKSRC}/LICENCE

USES=		gmake ncurses

PLIST_FILES=	bin/tweak \
		man/man1/tweak.1.gz

.include <bsd.port.mk>

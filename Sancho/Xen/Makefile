# **************************************************************************
# * Copyright (c) Enjellic Systems Development, LLC. All rights reserved.
# *
# * Please refer to the file named Documentation/COPYRIGHT in the top of
# * the source tree for copyright and licensing information.
# **************************************************************************/

include Config.mk
TARGET_CFLAGS = ${PLATFORM}

INSTPATH = ${BUILD_INSTPATH}/share
ETCPATH  = ${BUILD_INSTPATH}/etc

SUBDIRS = HurdLib NAAAIM

export stubdom=y

CC	  = gcc
CC_SEARCH = ${shell ${CC} -print-search-dirs | sed -n -e 's/install: \(.*\)/\1/p'}

GCCLIB=-L ${shell dirname ${shell ${CC} -print-libgcc-file-name}} -lgcc

XEN_INCLUDE = ${shell pkg-config xenstore --cflags-only-I | \
	sed -e 's/-I//'}
ifeq (${XEN_INCLUDE},)
	XEN_INCLUDE = /usr/include
endif

MINIOS_URL    = git://xenbits.xen.org/mini-os.git
MINIOS_SANCHO = sancho_${BUILD_XEN_VERSION}
ifeq ("${BUILD_XEN_VERSION}","4.15")
MINIOS_VERSION = xen-RELEASE-4.15.4
endif
ifeq ("${BUILD_XEN_VERSION}","4.16")
MINIOS_VERSION = xen-RELEASE-4.16.3
endif

NEWLIB_VERSION = 1.16.0
NEWLIB_URL     = ftp://sourceware.org/pub/newlib/newlib-${NEWLIB_VERSION}.tar.gz
NEWLIB_TAR     = Build/newlib-${NEWLIB_VERSION}.tar.gz
NEWLIB	       = ${CURDIR}/newlib-root/x86_64-xen-elf

ZLIB_VERSION = 1.2.3
ZLIB_URL     = http://xenbits.xen.org/xen-extfiles/zlib-${ZLIB_VERSION}.tar.gz
ZLIB_TAR     = Build/zlib-${ZLIB_VERSION}.tar.gz
ZLIB	     = ${NEWLIB}/libz.a

PCI_VERSION = 2.2.9
PCI_URL	    = http://www.kernel.org/pub/software/utils/pciutils/pciutils-${PCI_VERSION}.tar.gz
PCI_TAR	    = Build/pciutils-${PCI_VERSION}.tar.gz
PCI_CONFIG  = ${CURDIR}/Build/pciutils.mk
PCI_INCLUDE = ${CURDIR}/Build/pciutils-config.h
PCI_LIB     = ${CURDIR}/pciutils/lib
PCI_CC	    = ${CC} -D__MINIOS__ -DHAVE_LIBC -isystem ${CURDIR}/mini-os/include/x86/x86_64 ${TARGET_CPPFLAGS} ${TARGET_CFLAGS}

TARGET_CPPFLAGS = -U __linux__ -U __FreeBSD__ -U __sun__ -nostdinc	\
	-isystem ${CURDIR}/mini-os/include				\
	-isystem ${CURDIR}/mini-os/include/posix			\
	-isystem ${CURDIR}/newlib-root/x86_64-xen-elf/include		\
	-isystem ${CC_SEARCH}/include					\
	-isystem ${CURDIR}/mini-os/include/x86

NEWLIB_CPPFLAGS = -isystem ${CURDIR}/mini-os -D__MINIOS__ -DHAVE_LIBC	\
	-D_I386MACH_ALLOW_HW_INTERRUPTS ${TARGET_CPPFLAGS}

TARGET_LDFLAGS = -nostdlib -L ${CURDIR}/newlib-root/x86_64-xen-elf/lib

ZLIB_CPPFLAGS = -D__MINIOS__ -DHAVE_LIBC ${TARGET_CPPFLAGS}


#
# Generic build targets.
#
.PHONY: mini-os-build Sancho ${SUBDIRS}

all: mini-os newlib.stamp zlib.stamp pciutils.stamp ${SUBDIRS} Sancho \
	mini-os-build


#
# newlib targets.
#
newlib.stamp: ${NEWLIB}/lib/libc.a
	touch $@;

${NEWLIB}/lib/libc.a: mini-os.stamp newlib
	[ -d newlib-build ] || mkdir newlib-build;
	[ -d newlib-root ]  || mkdir newlib-root;
	( cd newlib-build && \
	  CC_FOR_TARGET="${CC} ${NEWLIB_CPPFLAGS} ${TARGET_CFLAGS} ${NEWLIB_CFLAGS}" \
	  AR_FOR_TARGET="ar" LD_FOR_TARGET="ld" RANLIB_FOR_TARGET="ranlib" \
	  ../newlib/configure --prefix="${CURDIR}/newlib-root" --verbose   \
	  --target=x86_64-xen-elf --enable-newlib-io-long-long		   \
	  --disable-multilib	&& \
	  ${MAKE}		&& \
	  ${MAKE} install );

newlib: ${NEWLIB_TAR}
	tar xvf $<;
	patch -p0 < Build/newlib.patch;
	mv newlib-${NEWLIB_VERSION} newlib;
	touch $@;

${NEWLIB_TAR}:
	wget -O $@ ${NEWLIB_URL};


#
# zlib targets.
#
zlib.stamp: ${NEWLIB}/lib/libz.a
	touch $@

${NEWLIB}/lib/libz.a: zlib mini-os.stamp
	( cd zlib &&						 	 \
	  CFLAGS="${ZLIB_CPPFLAGS} ${TARGET_CFLAGS}" CC=${CC}		 \
	  ./configure --prefix="${CURDIR}/newlib-root/x86_64-xen-elf" && \
	  ${MAKE} libz.a      			       		      && \
	  ${MAKE} install );

zlib: ${ZLIB_TAR}
	tar xvf $<;
	mv zlib-${ZLIB_VERSION} zlib;
	touch $@;

${ZLIB_TAR}:
	wget -O $@ ${ZLIB_URL};


#
# zlib targets.
#
pciutils.stamp: ${NEWLIB}/lib/libpci.a
	touch $@;

${NEWLIB}/lib/libpci.a: pciutils mini-os.stamp
	( cd pciutils					&& \
	  cp ${PCI_CONFIG} ${PCI_LIB}/config.mk		&& \
	  cp ${PCI_INCLUDE} ${PCI_LIB}/config.h		&& \
	  ${MAKE} CC="${PCI_CC}" lib/libpci.a		&& \
	  cp ${PCI_LIB}/libpci.a ${NEWLIB}/lib		&& \
	  cp ${PCI_LIB}/config.h ${NEWLIB}/include	&& \
	  cp ${PCI_LIB}/header.h ${NEWLIB}/include	&& \
	  cp ${PCI_LIB}/types.h  ${NEWLIB}/include );

pciutils: ${PCI_TAR}
	tar xvf $<;
	patch -p0 < Build/pciutils.patch;
	mv pciutils-${PCI_VERSION} pciutils;
	touch $@;

${PCI_TAR}:
	wget -O $@ ${PCI_URL};


#
# Sancho modeling code target.
#
Sancho:
	${MAKE} -C $@;


#
# mini-os targets.
#
mini-os-build: mini-os
	MINIOS_CONFIG="${CURDIR}/Sancho/sancho.cfg"			     \
	MINIOS_ROOT="${CURDIR}/mini-os"					     \
	DEF_CPPFLAGS="${TARGET_CPPFLAGS} -isystem ${XEN_INCLUDE}"	     \
	DEF_CFLAGS="${TARGET_CFLAGS}" DEF_LDFLAGS="${TARGET_LDFLAGS}"	     \
	make -C mini-os APP_OBJS="${CURDIR}/Sancho/libSanchoXen.a	     \
		${CURDIR}/HurdLib/libHurdLib.a ${CURDIR}/NAAAIM/libNAAAIM.a" \
		EXTRA_OBJS="${GCCLIB}";
	cp mini-os/mini-os Sancho/sancho.elf;

mini-os.stamp: mini-os
	[ ! -e mini-os/include/x86/mini-os -o \
	  ! -e mini-os/include/mini-os ] && make -C mini-os links && \
	touch mini-os.stamp || true;

mini-os:
	git clone -v ${MINIOS_URL};
	( cd $@ 					      && \
		git branch ${MINIOS_SANCHO} ${MINIOS_VERSION} && \
		git checkout ${MINIOS_SANCHO} );


#
# Sub-directory targets.
#
HurdLib: mini-os.stamp
	${MAKE} -C $@;

NAAAIM: mini-os.stamp
	${MAKE} -C $@;


#
# Installation targets.
#
install-bin:
	install Sancho/sancho.elf ${INSTPATH}/sancho-xen.elf;
	install Sancho/sancho.xen ${ETCPATH}/sancho.xen;


#
# Utility targets.
#
clean:
	set -e; for i in ${SUBDIRS}; do ${MAKE} -C $$i clean; done;
	[ -d zlib ] && make -C zlib clean || true;
	[ -d pciutils ] && make -C pciutils clean || true;
	[ -d mini-os ] && make -C mini-os clean || true;
	rm -f mini-os.stamp;
	make -C Sancho clean;

distclean: clean
	set -e; for i in ${SUBDIRS}; do ${MAKE} -C $$i distclean; done;
	[ -d newlib-build ] && rm -frv newlib-build || true;
	[ -d newlib-root ] && rm -frv newlib-root || true;
	[ -d newlib ] && rm -frv newlib || true;
	[ -d zlib ] && rm -rvf zlib || true;
	[ -d pciutils ] && rm -frv pciutils || true;
	rm -f *.stamp;
	make -C Sancho distclean;

purge: distclean
	[ -d mini-os ] && rm -frv mini-os || true;
	rm -frv Build/*.tar.gz;

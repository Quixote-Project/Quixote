# **************************************************************************
# * Copyright (c) Enjellic Systems Development, LLC. All rights reserved.
# *
# * Please refer to the file named Documentation/COPYRIGHT in the top of
# * the source tree for copyright and licensing information.
# **************************************************************************


#
# Variable declarations.
#
INSTPATH = ${BUILD_INSTPATH}/share

export MCUCFGDIR=${shell pwd}

SUBDIRS = HurdLib NAAAIM NRF52840-DK NRF52840-DONGLE


#
# Target directives.
#
.PHONY: ${SUBDIRS}

all: ${SUBDIRS}


#
# Subdirectory targets.
#
HurdLib:
	${MAKE} -C $@;

NAAAIM:
	${MAKE} -C $@;

NRF52840-DK:
	${MAKE} -C $@;

NRF52840-DONGLE:
	${MAKE} -C $@;


#
# Installation targets.
#
install-bin:
	install NRF52840-DK/Sancho/sancho.elf ${INSTPATH}/sancho-nrf-dk.elf;
	install NRF52840-DONGLE/Sancho/sancho.elf \
		${INSTPATH}/sancho-nrf-dongle.elf;


#
# Utility targets.
#
clean:
	set -e; for i in ${SUBDIRS}; do ${MAKE} -C $$i clean; done;

distclean:
	set -e; for i in ${SUBDIRS}; do ${MAKE} -C $$i distclean; done;

# **************************************************************************
# * Copyright (c) Enjellic Systems Development, LLC. All rights reserved.
# *
# * Please refer to the file named Documentation/COPYRIGHT in the top of
# * the source tree for copyright and licensing information.
# **************************************************************************


#
# Variable declarations.
#
export MCUCFGDIR=${shell pwd}

SUBDIRS = stm32l4xx stm32l562


#
# Target directives.
#
.PHONY: ${SUBDIRS}

all: ${SUBDIRS}


#
# Subdirectory targets.
#
stm32l4xx:
	${MAKE} -C $@;

stm32l562:
	${MAKE} -C $@;


#
# Utility targets.
#
clean:
	set -e; for i in ${SUBDIRS}; do ${MAKE} -C $$i clean; done;

distclean:
	set -e; for i in ${SUBDIRS}; do ${MAKE} -C $$i distclean; done;

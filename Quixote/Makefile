# **************************************************************************
# * Copyright (c) Enjellic Systems Development, LLC. All rights reserved.
# *
# * Please refer to the file named Documentation/COPYRIGHT in the top of
# * the source tree for copyright and licensing information.
# **************************************************************************/


#
# Include global build definitions if this is a sub-directory build.
#
ifndef BUILD_CONFIG
include ${shell cd ..; pwd}/Build.mk
endif

KERNEL_VERSION = ${BUILD_KERNEL_VERSION}

# Variable declarations.
INSTPATH = ${BUILD_INSTPATH}

ifdef SRDE_PRODUCTION
TYPE = -DSRDE_PRODUCTION
endif

SGXDIR = ../SRDE
include ${SGXDIR}/Make.enclave

SANCHODIR = ../Sancho
SANCHOSGX = ${SANCHODIR}/SGX

CSRC = quixote.c quixote-us.c quixote-console.c quixote-export.c test-sancho.c

ifeq ($(findstring SGX,${BUILD_SANCHOS}),SGX)
CSRC := ${CSRC} quixote-sgx.c quixote-sgx-u.c
endif
ifeq ($(findstring Xen, ${BUILD_SANCHOS}),Xen)
CSRC := ${CSRC} quixote-xen.c test-sancho-xen.c
endif
ifeq ($(findstring Nordic, ${BUILD_SANCHOS}),Nordic)
CSRC := ${CSRC} quixote-mcu.c
endif

TOOLS = quixote quixote-us quixote-export quixote-console \
	test-sancho test-thread test-domain-creation

ifeq ($(findstring SGX,${BUILD_SANCHOS}),SGX)
TOOLS := ${TOOLS} quixote-sgx quixote-sgx-u
endif
ifeq ($(findstring Xen, ${BUILD_SANCHOS}),Xen)
CSRC := ${CSRC} quixote-xen test-sancho-xen
endif
ifeq ($(findstring Nordic, ${BUILD_SANCHOS}),Nordic)
CSRC := ${CSRC} quixote-mcu
endif

INSTALLBIN  = quixote-console
INSTALLSBIN = quixote quixote-us quixote-export

ifeq ($(findstring SGX,${BUILD_SANCHOS}),SGX)
INSTALLSBIN := ${INSTALLSBIN} quixote-sgx quixote-sgx-u
endif
ifeq ($(findstring Xen, ${BUILD_SANCHOS}),Xen)
INSTALLSBIN := ${INSTALLSBIN} quixote-xen
INSTALLSCRIPTS = setup-quixote-xen
endif
ifeq ($(findstring Nordic, ${BUILD_SANCHOS}),Nordic)
INSTALLSBIN := ${INSTALLSBIN} quixote-mcu
endif

ifeq ("${KERNEL_VERSION}","5.4")
DEFINES = -DCAP_ML=38
endif
ifeq ("${KERNEL_VERSION}","5.15")
DEFINES = -DCAP_ML=41
endif
ifeq ("${KERNEL_VERSION}","6.x")
DEFINES = -DCAP_ML=41
endif

CFLAGS = ${BUILD_CFLAGS} ${DEFINES}

LDFLAGS = ${BUILD_LDFLAGS}


#
# Compilation directives.
#
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@;


#
# Automatic definition of classes and objects.
#
COBJS = ${CSRC:.c=.o}

HURD_LIBRARY = ../HurdLib/libHurdLib.a
HURDLIB	     = -L ../HurdLib -lHurdLib

NAAAIM_LIBRARY = ../lib/libNAAAIM.a
NAAAIMLIB      = -L ../lib -lNAAAIM

SEDE_LIBRARY = ../libSRDEruntime.a
SEDELIB	     = -L ${SGXDIR} -lSRDEruntime

MOSQUITTO_LIB = -L ../Support/mosquitto/lib -l mosquitto -lssl \
	${BUILD_LIBCRYPTO}

# libcap library
LIBCAP = -L ../support/libcap -lcap

LIBS	= ${NAAAIMLIB} ${HURDLIB} ${LIBCAP} ${BUILD_LIBCRYPTO}
LIBDEPS = ${HURD_LIBRARY} ${NAAAIM_LIBRARY}

ifeq ($(findstring Xen, ${BUILD_SANCHOS}), Xen)
XENLIBS = ${shell pkg-config --libs xenstore}  \
	${shell pkg-config --libs xengnttab}   \
	${shell pkg-config --libs xentoolcore} \
	${shell pkg-config --libs xentoollog}  \
	${shell pkg-config --libs xenevtchn} -lpthread
endif

MODELDEPS = ../SecurityModel/COE.o ../SecurityModel/Cell.o		  \
	../SecurityModel/SecurityPoint.o ../SecurityModel/SecurityEvent.o \
	../SecurityModel/TSEM.o ../SecurityModel/EventModel.o		  \
	../SecurityModel/EventParser.o

CFLAGS := ${CFLAGS} -I .. -I ../SecurityModel -I ../lib -I ../HurdLib \
	-I ${SGXDIR} -I ${SANCHOSGX} ${TYPE}

ifeq ($(findstring Xen, ${BUILD_SANCHOS}), Xen)
XENCFLAGS = ${shell pkg-config --cflags xenstore}
endif


#
# Target directives.
#
.PHONY: all tools

# Targets
all: ${COBJS} tools

tools: ${TOOLS}

quixote: quixote.o ${LIBDEPS}
	${CC} ${LDFLAGS} -o $@ $< ${LIBS};

quixote-mcu: quixote-mcu.o ${LIBDEPS} ${MODELDEPS}
	${CC} ${LDFLAGS} -o $@ $< ${MODELDEPS} ${LIBS};

quixote-us: quixote-us.o ${LIBDEPS} ${MODELDEPS}
	${CC} ${LDFLAGS} -o $@ $< ${MODELDEPS} ${LIBS};

quixote-sgx: quixote-sgx.o ${SANCHOSGX}/SanchoSGX.o \
	../SecurityModel/SecurityPoint.o ${LIBDEPS}
	${CC} ${LDFLAGS} -o $@ $< ${SANCHOSGX}/SanchoSGX.o	    \
		../SecurityModel/SecurityPoint.o ${SEDELIB} ${LIBS} \
		${BUILD_ELFLIB};

quixote-sgx-u: quixote-sgx-u.o ${SANCHOSGX}/SanchoSGX.o \
	../SecurityModel/SecurityPoint.o ${LIBDEPS}
	${CC} ${LDFLAGS} -o $@ $< ${SANCHOSGX}/SanchoSGX.o \
		../SecurityModel/SecurityPoint.o ${SEDELIB} ${LIBS} \
		${BUILD_ELFLIB};

quixote-xen: quixote-xen.o ${LIBDEPS} ${MODELDEPS}
	${CC} ${LDFLAGS} -o $@ $< ${MODELDEPS} ${LIBS} ${XENLIBS};

quixote-xen.o: quixote-xen.c
	${CC} ${CFLAGS} ${XENCFLAGS} -c $< -o $@;

quixote-export: quixote-export.o ${LIBDEPS} ${MODELDEPS}
	${CC} ${LDFLAGS} -o $@ $< ${MODELDEPS} ${LIBS} ${MOSQUITTO_LIB};

quixote-console: quixote-console.o ${LIBDEPS}
	${CC} ${LDFLAGS} -o $@ $< ${LIBS};

test-sancho: test-sancho.o ${LIBDEPS}
	${CC} ${LDFLAGS} -o $@ $< ${LIBS};

test-thread: test-thread.o ${LIBDEPS}
	${CC} ${LDFLAGS} -o $@ $< ${LIBS} -lpthread;

test-sancho-xen: test-sancho-xen.o ${LIBDEPS}
	${CC} ${LDFLAGS} -o $@ $< ${LIBS} ${XENLIBS};

sancho-enclave.h: ${SANCHOSGX}/SanchoSGX.signed.so
	${SGXDIR}/generate-array -i ${SANCHOSGX}/SanchoSGX.signed.so \
		-n Enclave > $@ || rm $@;

test-domain-creation: test-domain-creation.o ${LIBDEPS}
	${CC} ${LDFLAGS} -o $@ $< ${LIBS};

install-bin:
	install -s ${INSTALLBIN} ${INSTPATH}/bin;
	install -s ${INSTALLSBIN} ${INSTPATH}/sbin;
	[ -n "${INSTALLSCRIPTS}" ] && \
		install ${INSTALLSCRIPTS} ${INSTPATH}/sbin || true;

clean:
	rm -f *.o *~ sancho-enclave.h;
	rm -f ${TOOLS};

distclean: clean


# Source dependencies.
quixote.o: quixote.h sancho-cmd.h
quixote-us.o: quixote.h sancho-cmd.h
quixote-mcu.o: quixote.h sancho-cmd.h
quixote-sgx-u.o: quixote.h sancho-cmd.h sancho-enclave.h
quixote-console.o: sancho-cmd.h
test-sancho.o: sancho-cmd.h

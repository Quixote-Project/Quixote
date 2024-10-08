# **************************************************************************
# * Copyright (c) Enjellic Systems Development, LLC. All rights reserved.
# *
# * Please refer to the file named Documentation/COPYRIGHT in the top of
# * the source tree for copyright and licensing information.
# **************************************************************************/


# Variable declarations.
INSTPATH       = ${BUILD_INSTPATH}
INSTALLENCLAVE = test-naaaim.signed.so test-Duct.signed.so \
	test-Possum.signed.so
INSTINCLUDE    = IDtoken.h NAAAIM.h PossumPacket.h PossumPipe.h \
	RandomBuffer.h SEALkey.h SEALEDblob.h Report.h Attestation.h

ifdef SRDE_PRODUCTION
TYPE = -DSRDE_PRODUCTION
endif

CSRC   = test-naaaim.c
ENCSRC = test-naaaim-enclave.c test-naaaim-interface.c
LIBSRC = SHA256.c RandomBuffer.c Curve25519.c SHA256_hmac.c AES256_cbc.c   \
	Duct.c SRDEquote.c Base64.c IDtoken.c PossumPacket.c PossumPipe.c  \
	SoftwareStatus.c OTEDKS.c IDmgr.c Ivy.c RSAkey.c SEALkey.c	   \
	SEALEDblob.c X509cert.c Prompt.c AES128_cmac.c Report.c SRDEpipe.c \
	Attestation.c ssl-ocall.c
ASRC   = rdrand.s

DUCTSRC	   = test-Duct-enclave.c test-Duct-interface.c
POSSUMSRC  = test-Possum-enclave.c test-Possum-interface.c RandomBuffer.c
POSSUM2SRC = test-Possum2-enclave.c test-Possum2-interface.c RandomBuffer.c

LIBNAME	   = SRDEnaaaim
LIBRARY	   = lib${LIBNAME}.a
NAAAIMDIR  = ../../../lib

ENCLAVE_CC = gcc

SGXDIR = ../..
include ${SGXDIR}/Make.enclave

SGX_SSL	= /opt/Quixote/share/intel/sgxssl
SGX_INCLUDE = -I . -I ${SGX_SDK}/include -I ${SGX_SDK}/include/tlibc \
	-I ${SGX_SDK}/include/stlport -I ../SRDEfusion -I ../../../lib \
	-I ../.. -I ${SGX_SSL}/include

ENCLAVE_CFLAGS = -Wall -nostdinc -fvisibility=hidden -fpie -fstack-protector \
	${SGX_INCLUDE} -fno-builtin ${TYPE}

SGX_LIBRARY_PATH = ${SGX_SDK}/lib64
SSL_LIBRARY_PATH = ${SGX_SSL}/lib64

SGX_TRTS_LIB	    = sgx_trts
SGX_CRYPTO_LIB	    = sgx_tcrypto
SGX_SERVICE_LIBRARY = sgx_tservice

ENCLAVE_LDFLAGS = -Wl,--no-undefined -nostdlib -nodefaultlibs -nostartfiles   \
	-Wl,--whole-archive -lsgx_tsgxssl -Wl,--no-whole-archive	      \
	-L${SGX_LIBRARY_PATH} -L${SSL_LIBRARY_PATH} -L . -L ../SRDEfusion     \
	-Wl,--whole-archive						      \
	-l${SGX_TRTS_LIB} -lSRDEfusion					      \
	-Wl,--no-whole-archive						      \
	-Wl,--start-group						      \
	-lsgx_tstdc -l${SGX_CRYPTO_LIB} -l${SGX_SERVICE_LIBRARY} -lSRDEnaaaim \
	-lsgx_tsgxssl_crypto						      \
	-Wl,--end-group							      \
	-Wl,-Bstatic -Wl,-Bsymbolic -Wl,--no-undefined			      \
	-Wl,-pie,-eenclave_entry -Wl,--export-dynamic			      \
	-Wl,--defsym,__ImageBase=0					      \
	-Wl,--version-script=ldscript


TOOLS = test-naaaim test-Duct test-Possum test-Possum2
INSTALLBIN = ${TOOLS}

# C++TOOLS = test-naaaim-c++
ifneq (${CC}, musl-gcc)
TOOLS += ${C++TOOLS}
endif

CFLAGS = -Wall -O2 -fpic -fomit-frame-pointer -march=core2 -I ../.. \
	-I ../../.. -I ../../../HurdLib ${TYPE}

LDFLAGS = ${BUILD_LDFLAGS}

HURD_LIBRARY = ../../../HurdLib/libHurdLib.a
HURDLIB	     = -L ../../../HurdLib -lHurdLib

NAAAIM_LIBRARY = ../../../lib/libNAAAIM.a
NAAAIMLIB      = -L ../../../lib -lNAAAIM

RDK_LIBRARY = ../../libSRDEruntime.a
RDKLIB	    = -L ../../ -lSRDEruntime

LIBRARY_DEPENDS = ${HURD_LIBRARY} ${NAAAIM_LIBRARY} ${RDK_LIBRARY}
LIBS		= ${RDKLIB} ${BUILD_ELFLIB} ${NAAAIMLIB} ${HURDLIB}


#
# Compilation directives.
#
%.o: %.c
	${ENCLAVE_CC} ${ENCLAVE_CFLAGS} -c $< -o $@;

%.o: ../../../lib/%.c
	$(ENCLAVE_CC) $(ENCLAVE_CFLAGS) -c $< -o $@;

%.o: %.s
	${AS} -o $@ $<;


#
# Automatic definition of classes and objects.
#
COBJS	    = ${CSRC:.c=.o}
LIBOBJS	    = ${LIBSRC:.c=.o} ${ASRC:.s=.o}
ENCOBJS	    = ${ENCSRC:.c=.o}
DUCTOBJS    = ${DUCTSRC:.c=.o}
POSSUMOBJS  = ${POSSUMSRC:.c=.o}
POSSUM2OBJS = ${POSSUM2SRC:.c=.o}


#
# Target directives.
#
.PHONY: all tools enclave LDscript


# Targets
all: ${LIBRARY} enclave tools

enclave: ldscript test-naaaim.signed.so test-Duct.signed.so \
	test-Possum.signed.so test-Possum2.signed.so

test-naaaim.signed.so: test-naaaim.so
	${SGX_SIGNER} sign -key ${SGXKEY} -enclave $^ -out $@ \
		-config test-naaaim-enclave.xml;

test-naaaim.so: ${ENCOBJS} ${LIBRARY}
	${ENCLAVE_CC} -o $@ ${ENCOBJS} ${ENCLAVE_LDFLAGS};

test-Duct.signed.so: test-Duct.so
	${SGX_SIGNER} sign -key ${SGXKEY} -enclave $^ -out $@ \
		-config test-Duct-enclave.xml;

test-Duct.so: ${DUCTOBJS} ${LIBRARY}
	${ENCLAVE_CC} -o $@ ${DUCTOBJS} ${ENCLAVE_LDFLAGS};

test-Possum.signed.so: test-Possum.so
	${SGX_SIGNER} sign -key ${SGXKEY} -enclave $^ -out $@ \
		-config test-Possum-enclave.xml;

test-Possum.so: ${POSSUMOBJS} ${LIBRARY}
	${ENCLAVE_CC} -o $@ ${POSSUMOBJS} ${ENCLAVE_LDFLAGS};

test-Possum2.signed.so: test-Possum2.so
	${SGX_SIGNER} sign -key ${SGXKEY} -enclave $^ -out $@ \
		-config test-Possum-enclave.xml;

test-Possum2.so: ${POSSUM2OBJS} ${LIBRARY}
	${ENCLAVE_CC} -o $@ ${POSSUM2OBJS} ${ENCLAVE_LDFLAGS};

ldscript:
	echo "{"			>  ldscript;
	echo "global:"			>> ldscript;
	echo "g_global_data_sim;"	>> ldscript;
	echo "g_global_data;"		>> ldscript;
	echo "enclave_entry;"		>> ldscript;
	echo "local:"			>> ldscript;
	echo "*;"			>> ldscript;
	echo "};"			>> ldscript;


tools: ${TOOLS}

test-naaaim: test-naaaim.o ${LIBRARY_DEPENDS}
	${CC} ${LDFLAGS} -o $@ $< ${LIBS} ${BUILD_LIBCRYPTO};

test-naaaim.o: test-naaaim.c
	${CC} ${CFLAGS} -c $< -o $@;

test-naaaim-c++: test-naaaim-c++.o ${RDK_LIBRARY} ${HURD_LIBRARY}
	${CXX} ${LDFLAGS} -o $@ $< ${LIBS} ${BUILD_LIBCRYPTO};

test-naaaim-c++.o: test-naaaim-c++.cpp
	${CXX} ${CFLAGS} -c $< -o $@;

test-Duct: test-Duct.o ${LIBRARY_DEPENDS}
	${CC} ${LDFLAGS} -o $@ $< ${LIBS} ${BUILD_LIBCRYPTO};

test-Duct.o: test-Duct.c
	${CC} ${CFLAGS} -I ../../../lib -c $< -o $@;

test-Possum: test-Possum.o ${LIBRARY_DEPENDS}
	${CC} ${LDFLAGS} -o $@ $< ${LIBS} ${BUILD_LIBCRYPTO};

test-Possum.o: test-Possum.c
	${CC} ${CFLAGS} -I ../../../lib -c $< -o $@;

test-Possum2: test-Possum2.o ${LIBRARY_DEPENDS}
	${CC} ${LDFLAGS} -o $@ $< ${LIBS} ${BUILD_LIBCRYPTO};

test-Possum2.o: test-Possum2.c
	${CC} ${CFLAGS} -I ../../../lib -c $< -o $@;

${LIBRARY}: ${LIBOBJS}
	ar r ${LIBRARY} $^;
	ranlib ${LIBRARY};

install-bin:
	[ -d ${INSTPATH}/bin ] || mkdir -p ${INSTPATH}/bin;
	install -s ${INSTALLBIN} ${INSTPATH}/bin;
	[ -d ${INSTPATH}/lib/enclaves ] || mkdir -p ${INSTPATH}/lib/enclaves;
	install ${INSTALLENCLAVE} ${INSTPATH}/lib/enclaves;

install-dev:
	[ -d ${INSTPATH}/include ] || mkdir -p ${INSTPATH}/include;
	[ -d ${INSTPATH}/include/SRDEnaaaim ] || \
		mkdir -p ${INSTPATH}/include/SRDEnaaaim;
	install -m 644 ${INSTINCLUDE} ${INSTPATH}/include/SRDEnaaaim;
	[ -d ${INSTPATH}/lib ] || mkdir -p ${INSTPATH}/lib;
	install -m 644 ${LIBRARY} ${INSTPATH}/lib;

clean:
	rm -f *.o *~ ldscript;
	rm -f ${TOOLS};
	rm -f ${LIBRARY};

distclean: clean
	rm -f test-naaaim.so test-naaaim.signed.so test-Duct.so;
	rm -f test-Duct.signed.so test-Possum.so test-Possum.signed.so;
	rm -f test-Possum2.so test-Possum2.signed.so;


# Source dependencies.
test-naaaim-enclave.o: test-naaaim-interface.h RandomBuffer.h	\
	${NAAAIMDIR}/SHA256.h ${NAAAIMDIR}/Curve25519.h		\
	${NAAAIMDIR}/SHA256_hmac.h ${NAAAIMDIR}/AES256_cbc.h	\
	${NAAAIMDIR}/Base64.h
test-naaaim-interface.o: test-naaaim-interface.h

test-Duct-enclave.o: ${NAAAIMDIR}/Duct.h
test-Duct-interface.o: test-Duct-interface.h

test-Possum-enclave.o: test-Possum-interface.h RandomBuffer.h IDtoken.h	   \
	PossumPipe.h ${NAAAIMDIR}/Ivy.h ${NAAAIMDIR}/SHA256.h ../../SRDE.h \
	../SRDEfusion/SRDEfusion.h

test-Possum-interface.o: test-Possum-interface.h

SHA256.o: ${NAAAIMDIR}/SHA256.h
RandomBuffer.o: RandomBuffer.h
Curve25519.o: ${NAAAIMDIR}/Curve25519.h
SHA256_hmac.o: ${NAAAIMDIR}/SHA256_hmac.h
AES256_cbc.o: ${NAAAIMDIR}/AES256_cbc.h
Duct.o: ${NAAAIMDIR}/Duct.h
SRDEquote.o: ../../SRDEquote.h
Base64.o: ${NAAAIMDIR}/Base64.h
IDtoken.o: IDtoken.h
PossumPacket.o: PossumPacket.h RandomBuffer.h IDtoken.h	 \
	${NAAAIMDIR}/AES256_cbc.h ${NAAAIMDIR}/SHA256.h	 \
	${NAAAIMDIR}/SHA256_hmac.h ${NAAAIMDIR}/OTEDKS.h \
	${NAAAIMDIR}/Curve25519.h
PossumPipe.o: PossumPipe.h IDtoken.h ${NAAAIMDIR}/SHA256.h		    \
	${NAAAIMDIR}/SHA256_hmac.h ${NAAAIMDIR}/AES256_cbc.h RandomBuffer.h \
	${NAAAIMDIR}/Duct.h ${NAAAIMDIR}/SoftwareStatus.h		    \
	${NAAAIMDIR}/Curve25519.h PossumPacket.h ${NAAAIMDIR}/IDmgr.h	    \
	${NAAAIMDIR}/Ivy.h
SoftwareStatus.o: ${NAAAIMDIR}/SoftwareStatus.h ../../SRDE.h	\
	../SRDEfusion/SRDEfusion.h ${NAAAIMDIR}/SHA256.h
SoftwareStatus.o: ${NAAAIMDIR}/SoftwareStatus.h ../../SRDE.h	\
	../SRDEfusion/SRDEfusion.h ${NAAAIMDIR}/SHA256.h
OTEDKS.o: ${NAAAIMDIR}/OTEDKS.h ${NAAAIMDIR}/IDmgr.h ${NAAAIMDIR}/Ivy.h
IDmgr.o: IDtoken.h ${NAAAIMDIR}/IDmgr.h
Ivy.o: IDtoken.h ${NAAAIMDIR}/Ivy.h
SEALEDblob.o: NAAAIM.h SEALEDblob.h SEALkey.h
Report.o: NAAAIM.h Report.h

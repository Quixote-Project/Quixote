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

# Variable declarations.
INSTPATH      = ${BUILD_INSTPATH}
INSTALLBIN    = srde-gen-token
INSTALLSCRIPT = provision-epid provision-tokens provision-verifier

INSTALLDEV    = srde-sign
INSTINCLUDE   = PCEenclave.h PVEenclave.h QEenclave.h SRDEaesgcm.h SRDEcmac.h \
	SRDEecdsa.h SRDEepid.h SRDEloader.h SRDEmessage.h SRDEmetadata.h      \
	SRDEquote.h SRDErsa.h SRDEsigstruct.h SRDE.h SRDEenclave.h	      \
	SRDEocall.h SRDEfusion-ocall.h SRDEnaaaim-ocall.h SRDEpipe.h

SUBDIRS	    = SRDElibs Attestation Sample
DEV_SUBDIRS = SRDElibs Sample

CSRC = SRDEmetadata.c SRDEloader.c SRDEenclave.c SRDEsigstruct.c PVEenclave.c \
	SRDEmessage.c SRDEecdsa.c PCEenclave.c SRDEcmac.c SRDEaesgcm.c	      \
	SRDErsa.c  QEenclave.c SRDEepid.c SRDEquote.c SRDEquote_mgr.c	      \
	SRDEocall.c SRDEfusion-ocall.c SRDEnaaaim-ocall.c srde-exception.c    \
	srde-ocall.c SRDEpipe.c SRDEpipe_mgr.c SRDEfile_mgr.c

ASRC = srde-boot.s

LIBRARY = libSRDEruntime.a

TOOLS = srde-check srde-metadata srde-loader srde-load srde-sigstruct	\
	srde-gen-token test-ecall test-pcr srde-fandf srde-fandf-unibin \
	srde-provision srde-gen-verifier


LE	     = /opt/Quixote/share/intel/sgxpsw/aesm/libsgx_le.signed.so
LE_WHITELIST = /opt/Quixote/share/intel/sgxpsw/aesm/sgx_white_list_cert.bin
LE_SIGSTRUCT = /opt/Quixote/share/intel/sgxpsw/aesm/le_prod_css.bin

SGXKEY = ${CURDIR}/Signing-key.pem
export SGXKEY

AS = as

ifeq (${CC}, musl-gcc)
CINCLUDE = -I/usr/local/musl/include
else
CINCLUDE = -I/usr/local/ESD/include
endif

CDEBUG = -g -O2 -fomit-frame-pointer -march=core2
CFLAGS = -Wall -fpic ${CDEBUG} ${CINCLUDE}

LDFLAGS = ${BUILD_LDFLAGS}

# NAAAIM library definition
NAAAIM_LIB = -L../lib -lNAAAIM

# SGXrdk library definition.
RDKLIB = -L . -lSRDEruntime

# Sources for SGX include files, used by sdk-compute-size utility.
SGXSDK = /u/usr/src/SGX/linux-sgx
SGXSDK_INCLUDES = -I/opt/Quixote/share/intel/sgxsdk/include		\
	-I ${SGXSDK}/psw/ae/inc/internal -I ${SGXSDK}/external/epid-sdk \
	-I ${SGXSDK}/common/inc/internal -I ${SGXSDK}/psw/ae/pve


#
# Compilation directives.
#
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@;

%.o: %.s
	${AS} -o $@ $<;


#
# Automatic definition of classes and objects.
#
COBJS = ${CSRC:.c=.o}
AOBJS = ${ASRC:.s=.o}

HURDLIB = -L../lib -lHurdLib
INTEL_CRYPTO = -L/opt/Quixote/share/intel/sgxsdk/lib64 -lsgx_tcrypto

LIBS = ${RDKLIB} ${BUILD_ELFLIB} ${HURDLIB}

CFLAGS := ${CFLAGS} -I.. -I../lib -I../HurdLib


#
# Target directives.
#
.PHONY: all tools ${SUBDIRS}

# Targets
all: Signing-key.pem generate-array ${COBJS} ${AOBJS} ${LIBRARY} tools \
	${SUBDIRS}

${LIBRARY}: ${COBJS} ${AOBJS}
	ar r ${LIBRARY} $^;
	ranlib ${LIBRARY};

tools: ${TOOLS}

demos: srde-fandf srde-fandf-unibin

srde-check: srde-check.o
	${CC} ${LDFLAGS} -o $@ $^;

srde-metadata: srde-metadata.o
	${CC} ${LDFLAGS} -o $@ $^ ${LIBS} ${NAAAIM_LIB} ${BUILD_LIBCRYPTO};

srde-metadata.o: srde-metadata.c
	$(CC) $(CFLAGS) -c $< -o $@;

srde-loader: srde-loader.o ${LIBRARY}
	${CC} ${LDFLAGS} -o $@ $< ${LIBS};

srde-loader.o: srde-loader.c
	$(CC) $(CFLAGS) -c $< -o $@;

srde-load: srde-load.o ${LIBRARY}
	${CC} ${LDFLAGS} -o $@ $< ${LIBS};

srde-load.o: srde-load.c
	$(CC) $(CFLAGS) -c $< -o $@;

srde-sigstruct: srde-sigstruct.o ${LIBRARY}
	${CC} ${LDFLAGS} -o $@ $< ${LIBS};

srde-sigstruct.o: srde-sigstruct.c
	$(CC) $(CFLAGS) -c $< -o $@;

srde-gen-token: srde-gen-token.o
	${CC} ${LDFLAGS} -o $@ $^ ${LIBS} ${NAAAIM_LIB} ${BUILD_LIBCRYPTO};

srde-gen-token.o: srde-gen-token.c LE_whitelist.h
	$(CC) $(CFLAGS) -c $< -o $@;

srde-provision: srde-provision.o
	${CC} ${LDFLAGS} -o $@ $^ ${LIBS} ${NAAAIM_LIB} ${BUILD_LIBCRYPTO} \
		${INTEL_CRYPTO};

srde-provision.o: srde-provision.c
	$(CC) $(CFLAGS) -c $< -o $@;

sdk-compute-size: sdk-compute-size.o
	${CC} ${LDFLAGS} -o $@ $<;

sdk-compute-size.o: sdk-compute-size.c
	${CC} ${CFLAGS} ${SGXSDK_INCLUDES} -c $< -o $@;

generate-array: generate-array.o
	${CC} ${LDFLAGS} -o $@ $< ${HURDLIB};

generate-array.o: generate-array.c
	$(CC) $(CFLAGS) -c $< -o $@;

srde-gen-verifier: srde-gen-verifier.o
	${CC} ${LDFLAGS} -o $@ $^ ${LIBS} ${NAAAIM_LIB} ${BUILD_LIBCRYPTO};

srde-gen-verifier.o: srde-gen-verifier.c
	${CC} ${CFLAGS} -c $< -o $@;

test-ecall: test-ecall.o
	${CC} ${LDFLAGS} -o $@ $< ${LIBS};

test-pcr: test-pcr.o
	${CC} ${LDFLAGS} -static -o $@ $^ ${LIBS};

srde-fandf: srde-fandf.o
	${CC} -static ${LDFLAGS} -o $@ $^ ${LIBS} ${NAAAIM_LIB} \
		${BUILD_LIBCRYPTO};

srde-fandf.o: srde-fandf.c LE_whitelist.h LE.h
	$(CC) $(CFLAGS) -c $< -o $@;

srde-fandf-unibin: srde-fandf-unibin.o
	${CC} -static ${LDFLAGS} -o $@ $^ ${LIBS} ${NAAAIM_LIB} \
		${BUILD_LIBCRYPTO};

srde-fandf-unibin.o: srde-fandf-unibin.c LE_whitelist.h LE.h TE.h
	$(CC) $(CFLAGS) -c $< -o $@;

SRDEecdsa.o: SRDEecdsa.c
	$(CC) $(CFLAGS) -I /opt/Quixote/share/intel/sgxsdk/include -c $< -o $@;

SRDEcmac.o: SRDEcmac.c
	$(CC) $(CFLAGS) -I /opt/Quixote/share/intel/sgxsdk/include -c $< -o $@;

SRDEaesgcm.o: SRDEaesgcm.c
	$(CC) $(CFLAGS) -I /opt/Quixote/share/intel/sgxsdk/include -c $< -o $@;


sigstruct.h: generate-array
	./generate-array -i ${LE_SIGSTRUCT} -n LE_sigstruct > $@ || rm $@;

LE_whitelist.h: generate-array
	./generate-array -i ${LE_WHITELIST} -n LE_whitelist > $@ || rm $@;

LE.h: generate-array
	./generate-array -i ${LE} -n LE_image > $@ || rm $@;

TE.h: generate-array
	./generate-array -i TE.h -n TE_image > $@ || rm $@;

Signing-key.pem:
	openssl genrsa -3 -out $@ 3072;


#
# Subdirectory targets.
#
SRDElibs:
	${MAKE} -C $@;

Attestation:
	${MAKE} -C $@;

ISOidentity:
	${MAKE} -C $@;

Sample:
	${MAKE} -C $@;

install-bin:
	install -s ${INSTALLBIN} ${INSTPATH}/bin;

install-dev:
	[ -d ${INSTPATH}/include ] || mkdir -p ${INSTPATH}/include;
	[ -d ${INSTPATH}/include/SRDEruntime ] || \
		mkdir -p ${INSTPATH}/include/SRDEruntime;
	install -m 644 ${INSTINCLUDE} ${INSTPATH}/include/SRDEruntime;
	[ -d ${INSTPATH}/lib ] || mkdir -p ${INSTPATH}/lib;
	install -m 644 ${LIBRARY} ${INSTPATH}/lib;
	[ -d ${INSTPATH}/bin ] || mkdir -p ${INSTPATH}/bin;
	install -m 755 ${INSTALLDEV} ${INSTPATH}/bin;
	set -e; for dir in ${DEV_SUBDIRS}; do ${MAKE} -C $$dir $@; done;

tags:
	/opt/emacs/bin/etags *.{h,c};
clean:
	rm -f *.o *~;
	set -e; for i in ${SUBDIRS}; do ${MAKE} -C $$i clean; done
	rm -f ${LIBRARY};
	rm -f generate-array sdk-compute-size ${TOOLS};

distclean: clean;
	rm -f TAGS sigstruct.h LE_whitelist.h LE.h TE.h Signing-key.pem;
	set -e; for i in ${SUBDIRS}; do ${MAKE} -C $$i distclean; done


# Source dependencies.
srde-metadata.o: SRDE.h SRDEenclave.h SRDEsigstruct.h SRDEmetadata.h
srde-load.o: SRDEenclave.h
srde-provision.o: PVEenclave.h

SRDEmetadata.o: SRDE.h SRDEmetadata.h
SRDEloader.o: SRDE.h SRDEloader.h
SRDEenclave.o: SRDE.h SRDEenclave.h SRDEmetadata.h SRDEloader.h
SRDEsigstruct.o: SRDE.h SRDEsigstruct.h sigstruct.h
PVEenclave.o: SRDE.h SRDEenclave.h PVEenclave.h
SRDEmessage.o: SRDEmessage.h
QEenclave.o: SRDE.h SRDEenclave.h QEenclave.h
SRDEquote.o: SRDE.h QEenclave.h PCEenclave.h SRDEquote.h

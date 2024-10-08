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


# Installation path.
INSTPATH = ${BUILD_INSTPATH}


# Definitions for building libcap.
LIBCAP_VERSION = 2.66
LIBCAP_FILE    = libcap-${LIBCAP_VERSION}.tar.gz
LIBCAP_URL     = https://mirrors.edge.kernel.org/pub/linux/libs/security/linux-privs/libcap2/${LIBCAP_FILE}
LIBCAP_TAR     = Build/${LIBCAP_FILE}

# Definitions for building mosquitto.
MOSQUITTO_VERSION = 2.0.18
MOSQUITTO_FILE    = mosquitto-${MOSQUITTO_VERSION}.tar.gz
MOSQUITTO_URL     = https://mosquitto.org/files/source/${MOSQUITTO_FILE}
MOSQUITTO_TAR     = Build/${MOSQUITTO_FILE}


# Definitions for the mbedtls source package.
MBEDTLS_VERSION = 2.28.8
MBEDTLS_FILE    = mbedtls-${MBEDTLS_VERSION}.tar.gz
MBEDTLS_TAR     = Build/${MBEDTLS_FILE}
MBEDTLS_URL     = ${BUILD_MBEDURL}/v${MBEDTLS_VERSION}.tar.gz


# Definitions for the Nordic Software Development kit.
NORDIC_FILE = nRF5_SDK_17.1.0_ddde560.zip
NORDIC_ZIP  = Build/${NORDIC_FILE}
NORDIC_URL  = ${BUILD_NORDIC_URL}/${NORDIC_FILE}


#
# Generic build targets.
#
.PHONY: all

all: Build libcap.stamp mosquitto.stamp mbedtls.stamp # nordic.stamp


#
# libcap build.
#
libcap.stamp: libcap/libcap/libcap.a
	touch $@;

libcap/libcap/libcap.a: libcap
	make -C libcap CC=${CC} SHARED=no PAM_CAP=no GOLANG=no;

libcap: ${LIBCAP_TAR}
	tar xf $<;
	mv libcap-${LIBCAP_VERSION} libcap;

${LIBCAP_TAR}:
	wget -O $@ ${LIBCAP_URL};


#
# mosquitto build.
#
mosquitto.stamp: mosquitto/lib/libmosquitto.a
	touch $@;

mosquitto/lib/libmosquitto.a: mosquitto
	make -C mosquitto CC=${CC} WITH_CJSON=no WITH_STATIC_LIBRARIES=yes \
		LDFLAGS="`pkg-config libssl libcrypto --libs` -lrt";

mosquitto: ${MOSQUITTO_TAR}
	tar xf $<;
	mv mosquitto-${MOSQUITTO_VERSION} mosquitto;

${MOSQUITTO_TAR}:
	wget -O $@ ${MOSQUITTO_URL};


#
# mbedtls source.
#
mbedtls.stamp: mbedtls
	touch $@;

mbedtls: ${MBEDTLS_TAR}
	tar xf $<;
	mv mbedtls-${MBEDTLS_VERSION} mbedtls;
	touch mbedtls;

${MBEDTLS_TAR}:
	wget -O $@ ${MBEDTLS_URL};


#
# Nordic SDK sources.
#
nordic.stamp: nRF5_SDK
	touch $@;

nRF5_SDK: ${NORDIC_ZIP}
	unzip -x $<;
	mv nRF5_SDK_17.1.0_ddde560 nRF5_SDK;

${NORDIC_ZIP}:
	wget -O $@ ${NORDIC_URL};


#
# Target for Build directory.
#
Build:
	[ ! -d Build ] && mkdir Build || true;


#
# Installation target.
#
install-bin:
	install -s libcap/progs/capsh ${INSTPATH}/sbin;


#
# Utility targets.
#
clean:
	[ -d libcap ] && make -C libcap clean || true;
	[ -d mosquitto ] && make -C mosquitto clean || true;

distclean: clean
	[ -d libcap ] && rm -rf libcap || true;
	[ -d mosquitto ] && rm -rf mosquitto || true;
	[ -d mbedtls ] && rm -rf mbedtls || true;
	[ -d nRF5_SDK ] && rm -rf nRF5_SDK || true;
	rm -f *.stamp;

purge: distclean
	rm -f Build/*.tar.gz;
	rm -f Build/${NORDIC_FILE};
	rmdir Build;

# ***************************************************************************
# (C)Copyright 2003, The Open Hurderos Foundation. All rights reserved.
#
# Please see bottom of file for change information.
# ***************************************************************************


# Variable declarations.
CSRC = 	SHA256.c SHA256_hmac.c OrgID.c PatientID.c RandomBuffer.c  \
	IDtoken.c Authenticator.c AES256_cbc.c AuthenReply.c	   \
	IDqueryReply.c ProviderQuery.c SSLDuct.c 

# SERVERS = root-referral device-broker user-broker identity-broker \
# 	provider-server
SERVERS = root-referral device-broker user-broker

SUBDIRS = lib idgine utils edi # client

# CC = gcc
CC = musl-gcc

# Uncomment the following two lines to enable compilation with memory debug
# support
#
# DMALLOC = -DDMALLOC
# DMALLOC_LIBS = -ldmalloc

#
# Locations of SSL include files and libraries
#
SSL_INCLUDE = /usr/local/musl/include
SSL_LIBRARY = -L /usr/local/musl/lib -l ssl

#
# Locations for the Postgresql files and libraries.
#
POSTGRES_INCLUDE = /usr/local/pgsql/include
POSTGRES_LIBRARY = -L /usr/local/pgsql/lib -lpq

CDEBUG = -O2 -fomit-frame-pointer -march=pentium2 ${DMALLOC}
CDEBUG = -g ${DMALLOC}

CFLAGS = -Wall ${CDEBUG} -I./HurdLib # -pedantic-errors -ansi


LIBS = HurdLib

# LDFLAGS = -s -L/usr/local/krb5/lib 
LDFLAGS = -g ${DMALLOC_LIBS} -Wl,-rpath-link /usr/local/musl/lib -L./HurdLib \
	-L./lib


#
# Compilation directives.
#
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@;


#
# Automatic definition of classes and objects.
#
COBJS = ${CSRC:.c=.o}

LIBS = -l HurdLib -lNAAAIM

CFLAGS := ${CFLAGS} -I./HurdLib -I${SSL_INCLUDE} -I./lib


#
# Target directives.
#
.PHONY: client ${SUBDIRS}


# Targets
# all: ${COBJS} genrandom genid query-client servers ${SUBDIRS}
all: ${COBJS} genrandom query-client servers ${SUBDIRS}

servers: ${SERVERS} ${TOOLS}

root-referral: root-referral.o ${COBJS}
	${CC} ${LDFLAGS} -o $@ $^ ${LIBS} -lfl ${SSL_LIBRARY};

device-broker: device-broker.o ${COBJS}
	${CC} ${LDFLAGS} -o $@ $^ ${LIBS} -lfl ${SSL_LIBRARY};

user-broker: user-broker.o ${COBJS}
	${CC} ${LDFLAGS} -o $@ $^ ${LIBS} -lfl ${SSL_LIBRARY};

identity-broker: identity-broker.o DBduct.o OrgSearch.o ${COBJS}
	${CC} ${LDFLAGS} -o $@ $^ ${LIBS} -lrt -lfl ${SSL_LIBRARY} \
		${POSTGRES_LIBRARY};

provider-server: provider-server.o DBduct.o ${COBJS}
	${CC} ${LDFLAGS} -o $@ $^ ${LIBS} -lfl ${SSL_LIBRARY} \
		${POSTGRES_LIBRARY};

query-client: query-client.o ${COBJS}
	${CC} ${LDFLAGS} -o $@ $^ ${LIBS} -lfl ${SSL_LIBRARY};

genrandom: genrandom.o RandomBuffer.o SHA256.o
	${CC} ${LDFLAGS} -o $@ $^ ${LIBS} ${SSL_LIBRARY};

genid: genid.o ${COBJS} DBduct.o
	${CC} ${LDFLAGS} -o $@ $^ ${LIBS} -lfl ${SSL_LIBRARY} \
		${POSTGRES_LIBRARY};

gen-npi-search: gen-npi-search.o ${COBJS}
	${CC} ${LDFLAGS} -o $@ $^ ${LIBS} ${SSL_LIBRARY};

gen-brokerdb: gen-brokerdb.o ${COBJS} DBduct.o
	${CC} ${LDFLAGS} -o $@ $^ ${LIBS} ${SSL_LIBRARY} ${POSTGRES_LIBRARY};

token: token.o ${COBJS}
	${CC} ${LDFLAGS} -o $@ $^ ${LIBS} -lfl ${SSL_LIBRARY};

dotest: dotest.o ${COBJS}
	${CC} ${LDFLAGS} -o $@ $^ ${LIBS} ${SSL_LIBRARY};

ID_test: ID_test.o ${COBJS}
	${CC} ${LDFLAGS} -o $@ $^ ${LIBS} ${SSL_LIBRARY};

SSLDuct_test: SSLDuct_test.o ${COBJS}
	${CC} ${LDFLAGS} -o $@ $^ ${LIBS} ${SSL_LIBRARY}

DBduct_test: DBduct_test.o DBduct.o
	${CC} ${LDFLAGS} -o $@ $^ ${LIBS} ${POSTGRES_LIBRARY};

sha256key: sha256key.o SHA256.o
	${CC} ${LDFLAGS} -o $@ $^ ${LIBS} ${SSL_LIBRARY};

DBduct.o: DBduct.c
	$(CC) $(CFLAGS) -I${POSTGRES_INCLUDE} -c $< -o $@;


#
# Subdirectory targets.
#
client:
	${MAKE} -C $@;

idgine:
	${MAKE} -C $@;

lib:
	${MAKE} -C $@;

utils:
	${MAKE} -C $@;

tags:
	/opt/emacs/bin/etags *.{h,c};

clean:
	set -e; for i in ${SUBDIRS}; do ${MAKE} -C $$i clean; done
	rm -f *.o *~ TAGS;
	rm -f query-client
	rm -f ${SERVERS}
	rm -f genrandom genid token ID_test SSLDuct_test sha256key \
		gen-npi-search DBduct_test gen-brokerdb;


# Source dependencies.
SHA256.o: NAAAIM.h SHA256.h
SHA256_hmac.o: NAAAIM.h SHA256_hmac.h
OrgID.o: NAAAIM.h OrgID.h SHA256.h
PatientID.o: NAAAIM.h OrgID.h PatientID.h SHA256.h
RandomBuffer.o: NAAAIM.h RandomBuffer.h
IDtoken.o: NAAAIM.h IDtoken.h SHA256_hmac.h
SSLDuct.o: NAAAIM.h SSLDuct.h
Authenticator.o: NAAAIM.h Authenticator.h RandomBuffer.h IDtoken.h \
	AES256_cbc.h
AES256_cbc.o: AES256_cbc.h
AuthenReply.o: NAAAIM.h AuthenReply.h
OrgSearch.o: NAAAIM.h OrgSearch.h IDtoken.h
IDqueryReply.o: NAAAIM.h IDqueryReply.h RandomBuffer.h
DBDuct.o: NAAAIM.h DBduct.h
ProviderQuery.o: NAAAIM.h ProviderQuery.h

query-client.o: NAAAIM.h SSLDuct.h IDtoken.h Authenticator.h IDqueryReply.h \
	ProviderQuery.h

root-referral.o: NAAAIM.h SSLDuct.h IDtoken.h Authenticator.h AuthenReply.h \
	IDqueryReply.h
device-broker.o: NAAAIM.h SSLDuct.h IDtoken.h Authenticator.h SHA256.h \
	SHA256_hmac.h AuthenReply.h
user-broker.o: NAAAIM.h SSLDuct.h IDtoken.h Authenticator.h SHA256.h \
	SHA256_hmac.h AuthenReply.h
identity-broker.o: NAAAIM.h SSLDuct.h IDtoken.h Authenticator.h AuthenReply.h \
	OrgSearch.h IDqueryReply.h DBduct.h
provider-server.o: NAAAIM.h SSLDuct.h DBduct.h SHA256.h ProviderQuery.h

genid.o: NAAAIM.h SHA256.h SHA256_hmac.h OrgID.h PatientID.h \
	RandomBuffer.h DBduct.o
sha256key.o: NAAAIM.h SHA256.h

DBduct.o: DBduct.h

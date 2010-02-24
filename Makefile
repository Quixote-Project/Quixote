# ***************************************************************************
# (C)Copyright 2003, The Open Hurderos Foundation. All rights reserved.
#
# Please see bottom of file for change information.
# ***************************************************************************


# Variable declarations.
CSRC = 	SHA256.c SHA256_hmac.c RSAkey.c OrgID.c PatientID.c RandomBuffer.c \
	IDtoken.c Duct.c Authenticator.c AES256_cbc.c

SERVERS = root-referral device-broker user-broker

CC = gcc

# Uncomment the following two lines to enable compilation with memory debug
# support
#
# DMALLOC = -DDMALLOC
# DMALLOC_LIBS = -ldmalloc

#
# Locations of SSL include files and libraries
#
SSL_INCLUDE = /usr/local/ssl/include
SSL_LIBRARY = -L /usr/local/ssl/lib -l ssl

CDEBUG = -O2 -fomit-frame-pointer -march=pentium2 ${DMALLOC}
CDEBUG = -g ${DMALLOC}

CFLAGS = -Wall ${CDEBUG} -I./HurdLib # -pedantic-errors -ansi


LIBS = HurdLib

# LDFLAGS = -s -L/usr/local/krb5/lib 
LDFLAGS = -g ${DMALLOC_LIBS} -L./HurdLib


#
# Compilation directives.
#
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@;


#
# Automatic definition of classes and objects.
#
COBJS = ${CSRC:.c=.o}

LIBS = -l HurdLib

CFLAGS := ${CFLAGS} -I./HurdLib -I${SSL_INCLUDE}


# Targets
all: ${COBJS} genrandom genid query-client servers

servers: ${SERVERS}

root-referral: root-referral.o ${COBJS}
	${CC} ${LDFLAGS} -o $@ $^ ${LIBS} ${SSL_LIBRARY};

device-broker: device-broker.o ${COBJS}
	${CC} ${LDFLAGS} -o $@ $^ ${LIBS} ${SSL_LIBRARY};

user-broker: user-broker.o ${COBJS}
	${CC} ${LDFLAGS} -o $@ $^ ${LIBS} ${SSL_LIBRARY};

query-client: query-client.o ${COBJS}
	${CC} ${LDFLAGS} -o $@ $^ ${LIBS} ${SSL_LIBRARY};

genrandom: genrandom.o RandomBuffer.o SHA256.o
	${CC} ${LDFLAGS} -o $@ $^ ${LIBS} ${SSL_LIBRARY};

genid: genid.o ${COBJS}
	${CC} ${LDFLAGS} -o $@ $^ ${LIBS} -lfl ${SSL_LIBRARY};

token: token.o ${COBJS}
	${CC} ${LDFLAGS} -o $@ $^ ${LIBS} -lfl ${SSL_LIBRARY};

dotest: dotest.o ${COBJS}
	${CC} ${LDFLAGS} -o $@ $^ ${LIBS} ${SSL_LIBRARY};

RSAkey_test: RSAkey_test.o ${COBJS}
	${CC} ${LDFLAGS} -o $@ $^ ${LIBS} ${SSL_LIBRARY};

ID_test: ID_test.o ${COBJS}
	${CC} ${LDFLAGS} -o $@ $^ ${LIBS} ${SSL_LIBRARY};

Duct_test: Duct_test.o ${COBJS}
	${CC} ${LDFLAGS} -o $@ $^ ${LIBS} ${SSL_LIBRARY};

sha256key: sha256key.o SHA256.o
	${CC} ${LDFLAGS} -o $@ $^ ${LIBS} ${SSL_LIBRARY};

tags:
	/opt/emacs/bin/etags *.{h,c};

clean:
	rm -f *.o *~ TAGS;
	rm -f query-client
	rm -f ${SERVERS}
	rm -f genrandom genid token RSAkey_test ID_test Duct_test sha256key;


# Source dependencies.
SHA256.o: NAAAIM.h SHA256.h
SHA256_hmac.o: NAAAIM.h SHA256_hmac.h
RSAkey.o: NAAAIM.h RSAkey.h
OrgID.o: NAAAIM.h OrgID.h SHA256.h
PatientID.o: NAAAIM.h OrgID.h PatientID.h SHA256.h
RandomBuffer.o: NAAAIM.h RandomBuffer.h
IDtoken.o: NAAAIM.h IDtoken.h SHA256_hmac.h
Duct.o: NAAAIM.h Duct.h
Authenticator.o: NAAAIM.h Authenticator.h RandomBuffer.h RSAkey.h IDtoken.h \
	AES256_cbc.h
AES256_cbc.o: AES256_cbc.h

query-client.o: NAAAIM.h Duct.h IDtoken.h Authenticator.h

root-referral.o: NAAAIM.h Duct.h IDtoken.h Authenticator.h
device-broker.o: NAAAIM.h Duct.h IDtoken.h Authenticator.h SHA256.h \
	SHA256_hmac.h RSAkey.h
user-broker.o: NAAAIM.h Duct.h IDtoken.h Authenticator.h

genid.o: NAAAIM.h SHA256.h SHA256_hmac.h OrgID.h PatientID.h \
	RandomBuffer.h RSAkey.h
sha256key.o: NAAAIM.h SHA256.h

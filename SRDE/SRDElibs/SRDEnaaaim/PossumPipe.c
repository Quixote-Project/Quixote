/** \file
 * This file contains the implementation of an SGX based PossumPipe object.
 * This object provides a conduit for implementing secured
 * communication between two enclaves based on the identity and mutual
 * attestation state of the enclaves.
 */

/**************************************************************************
 * Copyright (c) Enjellic Systems Development, LLC. All rights reserved.
 *
 * Please refer to the file named Documentation/COPYRIGHT in the top of
 * the source tree for copyright and licensing information.
 **************************************************************************/

/* Local defines. */
#define IV_SIZE 16
#define ENCRYPTION_BLOCKSIZE 16
#define CHECKSUM_SIZE 32


/* Include files. */
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#if 0
#include <glob.h>
#endif

#include <openssl/asn1.h>
#include <openssl/asn1t.h>

#include <Origin.h>
#include <HurdLib.h>
#if 0
#include <Config.h>
#endif
#include <Buffer.h>
#include <String.h>
#if 0
#include <File.h>
#endif

#include <IDtoken.h>
#include <SHA256.h>
#include <SHA256_hmac.h>
#include <AES256_cbc.h>
#include <RandomBuffer.h>
#include <RSAkey.h>

#include "NAAAIM.h"
#include "Duct.h"
#include "SoftwareStatus.h"
#include "Curve25519.h"
#include "PossumPacket.h"
#include "IDmgr.h"
#include "Ivy.h"
#if 0
#include "TPMcmd.h"
#else
#include <SRDE.h>
#include <SRDEfusion.h>
#include <SRDEquote.h>
#endif
#include "PossumPipe.h"


/* Verify library/object header file inclusions. */
#if !defined(NAAAIM_LIBID)
#error Library identifier not defined.
#endif

#if !defined(NAAAIM_PossumPipe_OBJID)
#error Object identifier not defined.
#endif

/* State extraction macro. */
#define STATE(var) CO(PossumPipe_State, var) = this->state


/**
 * Pseudo-status for mode 2 authentication.
 */
static uint8_t mode2_status[32] = {
	0xec, 0x80, 0x17, 0x35, 0x76, 0x42, 0x42, 0x70, \
	0xf9, 0xb0, 0xab, 0xbd, 0xe5, 0x95, 0x12, 0x3d, \
	0x01, 0x5e, 0x48, 0x2e, 0xf8, 0xe2, 0x2e, 0x3a, \
	0xca, 0x76, 0x03, 0x60, 0xcc, 0xbf, 0x31, 0xd7
};


/**
 * Reference to the device identity provided.
 */
extern size_t Identity_size;
extern unsigned char *Identity;


/**
 * References to verified devices.
 */
extern Buffer Verifiers;


/** PossumPipe private state information. */
struct NAAAIM_PossumPipe_State
{
	/* The root object. */
	Origin root;

	/* Library identifier. */
	uint32_t libid;

	/* Object identifier. */
	uint32_t objid;

	/* Object status. */
	_Bool poisoned;

	/* Object error status. */
	PossumPipe_error_code error;

	/* Object debug status. */
	_Bool debug;

	/* Network connection object. */
	Duct duct;

	/* Remote software quote. */
	SRDEquote remote;

	/* Packet transmission nonce. */
	Sha256 nonce;

	/* Shared secrets. */
	Sha256 shared1;
	Sha256 shared2;

	/* Sent and received data extension hashes. */
	Sha256 sent;
	Sha256 received;

	/* Remote software status. */
	Buffer software;

	/* Nonce used to derive local software measurement. */
	uint8_t software_nonce[32];

	/* Mode specific client identity. */
	void *client_identity;
};


/**
 * The following definitions define the ASN1 encoding sequence for
 * the DER encoding of the packet which is transmitted over the wire.
 */
typedef struct {
	ASN1_INTEGER *type;
	ASN1_OCTET_STRING *nonce;
	ASN1_OCTET_STRING *payload;
} possum_packet;

ASN1_SEQUENCE(possum_packet) = {
	ASN1_SIMPLE(possum_packet, type,    ASN1_INTEGER),
	ASN1_SIMPLE(possum_packet, nonce,    ASN1_OCTET_STRING),
	ASN1_SIMPLE(possum_packet, payload, ASN1_OCTET_STRING),
} ASN1_SEQUENCE_END(possum_packet)

IMPLEMENT_ASN1_FUNCTIONS(possum_packet)


/**
 * Internal private method.
 *
 * The following function is a simply replacement for the srand() function
 * supplied by the standard C library.  It currently does nothing except
 * return in order to avoid elimination of the function with an ifdef
 * construct.
 */

static void srand(unsigned int unused)

{
	return;
}


/**
 * Internal private method.
 *
 * The following function is a replacement for the rand() function from
 * the standard C library.  It uses the rdrand() function supplied
 * by the NAAAIM library to generate a random value for the caller.
 */

extern _Bool rdrand(uint8_t *);

static unsigned int rand(void)

{
	uint8_t bufr[sizeof(uint64_t)];


	rdrand(bufr);
	return *(unsigned int *) bufr;
}


/**
 * Internal private method.
 *
 * This method is responsible for initializing the NAAAIM_PossumPipe_State
 * structure which holds state information for each instantiated object.
 *
 * \param S	A pointer to the object containing the state information which
 *        	is to be initialized.
 */

static void _init_state(CO(PossumPipe_State,S))

{
	S->libid = NAAAIM_LIBID;
	S->objid = NAAAIM_PossumPipe_OBJID;

	S->poisoned = false;
	S->error    = PossumPipe_error_internal;
	S->debug    = false;

	S->remote = NULL;

	memset(S->software_nonce, '\0', sizeof(S->software_nonce));

	S->client_identity = NULL;

	return;
}


/**
 * Internal private method.
 *
 * This method implements initialization of the object which will hold
 * the data used for the per packet nonce.  This is a SHA256 object
 * which will be used to generate 1-2 encryption blocks of data to be
 * loaded into the packet.
 *
 * \param S		A pointer to the object state of the object
 *			requesting generation of the nonce.
 *
 * \param chksum	The object which will contain the checksum.
 *
 * \return		A boolean value is returned to indicate the
 *			status of the checksum encryption.  A false value
 *			indicates an error occured during
 *			initialization of the nonce.  A true value
 *			indicates the nonce has been successfully
 *			initialized.
 */

static _Bool _setup_nonce(CO(PossumPipe_State, S))

{
	_Bool retn = false;

	Buffer b;

	RandomBuffer rb = NULL;


	INIT(NAAAIM, RandomBuffer, rb, goto done);

	/* Seed the random number generator. */
	if ( !rb->generate(rb, sizeof(unsigned int)) )
		ERR(goto done);
	b = rb->get_Buffer(rb);
	srand(*((unsigned int *) b->get(b)));


	/* Initialize the nonce generator. */
	if ( !rb->generate(rb, 2 * ENCRYPTION_BLOCKSIZE) )
		ERR(goto done);
	S->nonce->add(S->nonce, rb->get_Buffer(rb));
	if ( !S->nonce->compute(S->nonce) )
		ERR(goto done);

	retn = true;


 done:
	WHACK(rb);

	return retn;
}


/**
 * External public method.
 *
 * This method encapsulates all of the functionality needed to
 * initiate a PossumPipe running in server mode.
 *
 * \param this		A pointer to the object which is to be initialized
 *			as a server.
 *
 * \param host		The name or ID address of the interface which
 *			the server is to listen on.
 *
 * \param port		The port number which the server is to listen on.
 *
 * \param do_reverse	A flag to indicate whether or not reverse DNS
 *			lookups are to be done on the incoming connection.
 *			A false value inhibits reverse DNS lookups while
 *			a true value (default).
 *
 * \return		A boolean value is returned to indicate the
 *			status of the server setup.  A false value
 *			indicates that connection setup failed while a
 *			true value indicates the server is listening
 *			for connections.
 */

static _Bool init_server(CO(PossumPipe, this), CO(char *, host), \
			 const int port, const _Bool do_reverse)

{
	STATE(S);

	_Bool retn = false;


	if ( !S->duct->init_server(S->duct) )
		ERR(goto done);
	if ( (host != NULL) && !S->duct->set_server(S->duct, host) )
		ERR(goto done);
	if ( !S->duct->init_port(S->duct, NULL, port) )
		ERR(goto done);
	S->duct->do_reverse(S->duct, do_reverse);

	retn = true;


 done:
	return retn;
}


/**
 * External public method.
 *
 * This method encapsulates the functionality needed to initiate the
 * client side of a PossumPipe connection.
 *
 * \param this		A pointer to the object which is to be initialized
 *			as a client.
 *
 * \param host		The name or ID address of the server to be
 *			connected to.
 *
 * \param port		The port number on the server to which the
 *			connection is to be initiatedto.
 *
 * \return		A boolean value is returned to indicate the
 *			status of the connection.  A false value denotes
 *			connection setup failed while a true value
 *			indicates the setup was successful.
 */

static _Bool init_client(CO(PossumPipe, this), CO(char *, host), \
			 const int port)

{
	STATE(S);

	_Bool retn = false;


	if ( !S->duct->init_client(S->duct) )
		ERR(goto done);
	if ( !S->duct->init_port(S->duct, host, port) )
		ERR(goto done);

	retn = true;


 done:
	return retn;
}


/**
 * External public method.
 *
 * This method implements accepting a connection on an initialized server
 * port.  This is an inheritance interface to the Duct object imbeded
 * in this object.
 *
 * \param this	The object which is to accept the client connection.
 *
 * \return	This call blocks until a connection occurs.  If a
 *		connection is successfully established a true value is
 *		returned to the caller.  A false value indicates the
 *		connection was not successfully established.
 */

static _Bool accept_connection(CO(PossumPipe, this))

{
	STATE(S);

	return S->duct->accept_connection(S->duct);
}


/**
 * Internal private method.
 *
 * This method implements the computation of the HMAC checksum over
 * the supplied payload.
 *
 * \param S		A pointer to the object state of the object
 *			requesting checksum computation.
 *
 * \param bufr		The object containing the payload on which the
 *			checksum is to be computed.
 *
 * \param software	The buffer containing the software checksum to
 *			be used to generate the authentication key.
 *
 * \param chksum	The object which will contain the checksum.
 *
 * \return		A boolean value is returned to indicate the
 *			status of the checksum encryption.  A false value
 *			indicates an error occured during computation
 *			of the checksum.  A true value indicates the
 *			computation was successful and the output
 *			buffer contains a valid checksum.
 */

static _Bool _compute_checksum(CO(PossumPipe_State, S), CO(Buffer, payload), \
			       CO(Buffer, software), CO(Buffer, chksum))

{
	_Bool retn = false;

	unsigned char *p;

	Buffer b   = S->shared1->get_Buffer(S->shared1),
	       key = NULL;

	SHA256_hmac hmac = NULL;


	if ( (payload == NULL) || payload->poisoned(payload) )
		ERR(goto done);
	if ( (chksum == NULL) || chksum->poisoned(chksum) )
		ERR(goto done);

	/* Generate the key for the checksum. */
	INIT(HurdLib, Buffer, key, goto done);

	if ( b->size(b) <= IV_SIZE )
		ERR(goto done);
	p = b->get(b) + IV_SIZE;
	if ( !key->add(key, p, b->size(b) - IV_SIZE) )
		ERR(goto done);

	if ( (hmac = NAAAIM_SHA256_hmac_Init(key)) == NULL )
		ERR(goto done);
	hmac->add_Buffer(hmac, software);
	if ( !hmac->compute(hmac) )
		ERR(goto done);

	key->reset(key);
	if ( !key->add_Buffer(key, hmac->get_Buffer(hmac)) )
	     ERR(goto done);

	if ( S->debug ) {
		fputs("Checksum key:\n", stderr);
		key->print(key);
	}


	/* Compute the checksum over the packet payload with the key. */
	hmac->reset(hmac);
	hmac->add_Buffer(hmac, payload);
	if ( !hmac->compute(hmac) )
		ERR(goto done);

	if ( !chksum->add_Buffer(chksum, hmac->get_Buffer(hmac)) )
		ERR(goto done);

	retn = true;


 done:
	WHACK(key);
	WHACK(hmac);

	return retn;
}


/**
 * Internal private method.
 *
 * This method implements the verification of the checksum in the
 * supplied payload.
 *
 * \param S		A pointer to the object state of the object
 *			requesting checksum verification.
 *
 * \param debug		A flag used to indicated whether or not
 *			debugging is enabled in the communications
 *			object.
 *
 * \param packet	The object containing the payload containing
 *			the checksum to be verified.
 *
 * \return		A boolean value is returned to indicate the
 *			status of the checksum verification.  A false
 *			value indicates an error occured during verification
 *			of the checksum.  A true value indicates the
 *			computation was successful and the payload was
 *			verfified as valid.
 */

static _Bool _verify_checksum(CO(PossumPipe_State, S), CO(Buffer, packet))

{
	_Bool retn = false;

	size_t payload;

	Buffer computed = NULL,
	       incoming = NULL;


	if ( (packet == NULL) || packet->poisoned(packet) )
		ERR(goto done);

	/* Extract the incoming checksum. */
	INIT(HurdLib, Buffer, incoming, goto done);
	payload = packet->size(packet) - CHECKSUM_SIZE;
	if ( !incoming->add(incoming, packet->get(packet) + payload, \
			    CHECKSUM_SIZE) )
		ERR(goto done);
	packet->shrink(packet, CHECKSUM_SIZE);

	/* Compute the checksum over the packet body. */
	INIT(HurdLib, Buffer, computed, goto done);
	if ( !_compute_checksum(S, packet, S->software, computed) )
		ERR(goto done);
	if ( !incoming->equal(incoming, computed) )
		ERR(goto done);

	retn = true;


 done:
	WHACK(computed);
	WHACK(incoming);

	return retn;
}


/**
 * Internal private method.
 *
 * This method implements the generation of the initialization vector
 * and key for the supplied packet payload for the remote host.  This
 * vector and key are then used to encrypt the packet.
 *
 * \param S		A pointer to the object state of the object
 *			requesting encryption.
 *
 * \param bufr		The object containing the packet payload.  The
 *			contents of the object is replaced with the
 *			encrypted payload.
 *
 * \return		A boolean value is returned to indicate the
 *			status of packet encryption.  A false value
 *			indicates an error occured during
 *			encryption. A true value indicates the
 *			packet was successfully encrypted.
 */

static _Bool _encrypt_packet(CO(PossumPipe_State, S), CO(Buffer, payload))

{
	_Bool retn = false;

	Buffer b,
	       iv = NULL;

	AES256_cbc cipher = NULL;

	SHA256_hmac key = NULL;


	if ( (payload == NULL) || payload->poisoned(payload) )
		ERR(goto done);


	/* Extract the initialization vector from the first shared secret. */
	INIT(HurdLib, Buffer, iv, goto done);
	if ( !iv->add(iv, S->shared1->get(S->shared1), IV_SIZE) )
		ERR(goto done);

	/* Generate the encryption key. */
	b = S->shared2->get_Buffer(S->shared2);
	if ( (key = NAAAIM_SHA256_hmac_Init(b)) == NULL )
		ERR(goto done);
	key->add_Buffer(key, S->sent->get_Buffer(S->sent));
	if ( !key->compute(key) )
		ERR(goto done);
	if ( !S->sent->extend(S->sent, payload) )
		ERR(goto done);

	/* Encrypt the packet. */
	b = key->get_Buffer(key);
	if ( S->debug ) {
		fputs("Encrypt key:\n", stderr);
		b->print(b);
	}
	if ( (cipher = NAAAIM_AES256_cbc_Init_encrypt(b, iv)) == NULL )
		ERR(goto done);
	if ( cipher->encrypt(cipher, payload) == NULL )
		ERR(goto done);

	payload->reset(payload);
	if ( !payload->add_Buffer(payload, cipher->get_Buffer(cipher)) )
		ERR(goto done);

	retn = true;


done:
	WHACK(iv);
	WHACK(cipher);
	WHACK(key);

	return retn;
}


/**
 * External public method.
 *
 * This method implements sending a packet to the remote endpoint.
 * The supplied packet information is ASN1 encoded and the resulting
 * ASN1 data structure is encrypted and an HMAC trailing checksum
 * is added.  The resulting data structure is transmitted to the
 * endpoint.
 *
 * \param this		A pointer to the object which is to initiate
 *			the send.
 *
 * \param type		The type of packet to be sent.
 *
 * \param packet	The object containing the raw data to be
 *			transmited.
 *
 * \return		A boolean value is returned to indicate the
 *			status of packet transmission.  A false value
 *			indicates an error occured during
 *			transmission. A true value indicates the
 *			packet was successfully transmitted.
 */

static _Bool send_packet(CO(PossumPipe, this), const PossumPipe_type type, \
			 CO(Buffer, bufr))

{
	STATE(S);

	_Bool retn = false;

        unsigned char *asn = NULL;

        unsigned char **p = &asn;

	int asn_size;

	possum_packet *packet = NULL;

	Buffer b,
	       chksum = NULL;

	SoftwareStatus software = NULL;


	if ( S->poisoned )
		ERR(goto done);
	if ( (bufr == NULL) || bufr->poisoned(bufr) )
		ERR(goto done);

	/* ASN1 encode the packet. */
	if ( (packet = possum_packet_new()) == NULL )
		ERR(goto done);
	if ( ASN1_INTEGER_set(packet->type, type) != 1 )
		ERR(goto done);
	if ( ASN1_OCTET_STRING_set(packet->payload, bufr->get(bufr), \
				   bufr->size(bufr)) != 1 )
		ERR(goto done);

	/* Generate a per packet nonce. */
	b = S->nonce->get_Buffer(S->nonce);
	bufr->reset(bufr);
	bufr->add(bufr, b->get(b), rand() & 0x1f);

	if ( ASN1_OCTET_STRING_set(packet->nonce, bufr->get(b), \
				   bufr->size(b)) != 1 )
		ERR(goto done);
	if ( !S->nonce->rehash(S->nonce, (rand() & 0xf) + 1) )
		ERR(goto done);

        asn_size = i2d_possum_packet(packet, p);
        if ( asn_size < 0 )
                ERR(goto done);

	bufr->reset(bufr);
	if ( !bufr->add(bufr, asn, asn_size) )
		ERR(goto done);

	/* Encrypt the buffer contents and add an authentication checksum. */
	INIT(NAAAIM, SoftwareStatus, software, goto done);
	if ( !software->open(software) )
		ERR(goto done);
	if ( !software->measure_derived(software, S->software_nonce) )
		ERR(goto done);
	b = software->get_template_hash(software);

	INIT(HurdLib, Buffer, chksum, goto done);
	if ( !_encrypt_packet(S, bufr) )
		ERR(goto done);

	if ( !_compute_checksum(S, bufr, b, chksum) )
		ERR(goto done);
	bufr->add_Buffer(bufr, chksum);

	if ( !S->shared1->rehash(S->shared1, 1) )
		ERR(goto done);

	/* Send the processed buffer. */
	if ( !S->duct->send_Buffer(S->duct, bufr) )
		ERR(goto done);

	retn = true;


 done:
	WHACK(chksum);
	WHACK(software);

	if ( !retn )
		S->poisoned = true;
	if ( packet != NULL )
		possum_packet_free(packet);
	if ( asn != NULL )
		OPENSSL_free(asn);

	return retn;
}


/**
 * Internal private method.
 *
 * This method implements the generation of the initialization vector
 * and key for the supplied packet payload for the remote host.  This
 * vector and key are then used to decrypt the packet.
 *
 * \param S		A pointer to the object state of the object
 *			requesting decryption.
 *
 * \param bufr		The object containing the encrypted packet
 *			payload.  The contents of the object is
 *			replaced with the encrypted payload.
 *
 * \return		A boolean value is returned to indicate the
 *			status of payload decryption.  A false value
 *			indicates an error occured during
 *			decryption. A true value indicates the
 *			packet was successfully encrypted and the
 *			contents of the result object is valid.
 */

static _Bool _decrypt_packet(CO(PossumPipe_State, S), CO(Buffer, payload))

{
	_Bool retn = false;

	Buffer b,
	       iv = NULL;

	AES256_cbc cipher = NULL;

	SHA256_hmac key = NULL;


	if ( (payload == NULL) || payload->poisoned(payload) )
		ERR(goto done);


	/* Extract the initialization vector from the first shared secret. */
	INIT(HurdLib, Buffer, iv, goto done);
	if ( !iv->add(iv, S->shared1->get(S->shared1), IV_SIZE) )
		ERR(goto done);

	/* Generate the encryption key. */
	b = S->shared2->get_Buffer(S->shared2);
	if ( (key = NAAAIM_SHA256_hmac_Init(b)) == NULL )
		ERR(goto done);
	key->add_Buffer(key, S->received->get_Buffer(S->received));
	if ( !key->compute(key) )
		ERR(goto done);

	/* Decrypt the packet. */
	b = key->get_Buffer(key);
	if ( S->debug ) {
		fputs("Decrypt key:\n", stderr);
		b->print(b);
	}
	if ( (cipher = NAAAIM_AES256_cbc_Init_decrypt(b, iv)) == NULL )
		ERR(goto done);
	if ( cipher->decrypt(cipher, payload) == NULL )
		ERR(goto done);

	payload->reset(payload);
	if ( !payload->add_Buffer(payload, cipher->get_Buffer(cipher)) )
		ERR(goto done);
	if ( !S->received->extend(S->received, payload) )
		ERR(goto done);

	retn = true;


done:
	WHACK(iv);
	WHACK(cipher);
	WHACK(key);

	return retn;
}


/**
 * External public method.
 *
 * This method implements the reception and decoding of a packet from
 * a remote endpoint.  The raw packet is decrypted and authenticated
 * with the trailing checksum.  The ASN1 data structure is decoded and
 * loaded into the supplied buffer.
 *
 * \param this	A pointer to the object which is to initiate
 *		the send.
 *
 * \param bufr	The object which will be loaded with the packet payload.
 *
 * \return		An enumerated type is returned to indicate the
 *			status and type of the payload.
 */

static PossumPipe_type receive_packet(CO(PossumPipe, this), CO(Buffer, bufr))

{
	STATE(S);

	_Bool retn = false;

	PossumPipe_type status,
			remote_retn;

        unsigned char *asn = NULL;

        unsigned const char *p = asn;

	int asn_size;

	possum_packet *packet = NULL;


	if ( S->poisoned )
		ERR(goto done);
	if ( (bufr == NULL) || bufr->poisoned(bufr) )
		ERR(goto done);

	/* Receive and decode the packet. */
	if ( !S->duct->receive_Buffer(S->duct, bufr) ) {
		if ( !S->duct->eof(S->duct) )
			ERR(goto done);
		retn	    = true;
		remote_retn = PossumPipe_eop;
		goto done;

	}
	if ( bufr->size(bufr) == 0 ) {
		retn	    = true;
		remote_retn = PossumPipe_eop;
		goto done;
	}

	/* Decrypt the payload. */
	if ( !_verify_checksum(S, bufr) )
		ERR(goto done);
	if ( !_decrypt_packet(S, bufr) )
		ERR(goto done);
	if ( !S->shared1->rehash(S->shared1, 1) )
		ERR(goto done);

	/* Decode the packet. */
	p = bufr->get(bufr);
	asn_size = bufr->size(bufr);
        if ( !d2i_possum_packet(&packet, &p, asn_size) )
                ERR(goto done);

	remote_retn = ASN1_INTEGER_get(packet->type);

	bufr->reset(bufr);
	if ( !bufr->add(bufr, ASN1_STRING_get0_data(packet->payload), \
			ASN1_STRING_length(packet->payload)) )
		ERR(goto done);

	retn = true;


 done:
	if ( retn )
		status = remote_retn;
	else {
		S->poisoned = true;
		status = PossumPipe_failure;
	}

	if ( packet != NULL )
		possum_packet_free(packet);

	return status;
}


/**
 * Private function.
 *
 * This function is responsible for searching the list of attestable
 * clients based on the identification challenge.
 *
 * \param packet	The Buffer object containing the type 1 POSSUM
 *			packet which was received.
 *
 * \param token		The IDtoken object which will be loaded with
 *			the identity token which identifies the
 *			client.
 *
 * \param match		A pointer to the identify verifier object that
 *			will be loaded with the verifier which matches
 *			the client.
 *
 * \return		A true value is used to indicate the search
 *			for the client was successful.  A false value
 *			is returned if the search was unsuccessful.
 */

static _Bool find_client(CO(Buffer, packet), CO(IDtoken, token), \
			 CO(Ivy, match))

{
	_Bool retn = false;

	size_t lp,
	       cnt;

	Buffer b,
	       idkey	= NULL,
	       identity = NULL;

	Ivy ivy,
	    *ivyp;

	SHA256_hmac idf = NULL;


	/* Verify packet status. */
	if ( packet->poisoned(packet) )
		ERR(goto done);
	if ( Verifiers == NULL )
		ERR(goto done);
	if ( Verifiers->size(Verifiers) == 0 )
		ERR(goto done);


	/* Initialize objects to be used. */
	INIT(HurdLib, Buffer, idkey, ERR(goto done));
	INIT(HurdLib, Buffer, identity, ERR(goto done));


	/* Load the identity key/salt and the asserted client identity. */
	if ( !idkey->add(idkey, packet->get(packet), NAAAIM_IDSIZE) )
		ERR(goto done);
	if ( !identity->add(identity, packet->get(packet) + NAAAIM_IDSIZE, \
			    NAAAIM_IDSIZE) )
		ERR(goto done);
	if ( (idf = NAAAIM_SHA256_hmac_Init(idkey)) == NULL )
		ERR(goto done);


	/*
	 * Loop over the list of counter-party identities and determine
	 * which of the identities match the identity assertion issued
	 * by the POSSUM counter-party.
	 */
	ivyp = (Ivy *) Verifiers->get(Verifiers);
	cnt  = Verifiers->size(Verifiers) / sizeof(Ivy);

	for (lp= 0; lp < cnt; ++lp) {
		ivy = *ivyp;
		token->reset(token);

		/* Extract identity token. */
		if ( (b = ivy->get_element(ivy, Ivy_id)) == NULL )
			ERR(goto done);
		if ( !token->decode(token, b) )
			ERR(goto done);

		if ( (b = token->get_element(token, IDtoken_orgkey)) == NULL )
			ERR(goto done);
		idf->add_Buffer(idf, b);

		if ( (b = token->get_element(token, IDtoken_orgid)) == NULL )
			ERR(goto done);
		idf->add_Buffer(idf, b);

		if ( !idf->compute(idf) )
			ERR(goto done);

		if ( identity->equal(identity, idf->get_Buffer(idf)) ) {
			identity->reset(identity);
			if ( !ivy->encode(ivy, identity) )
				ERR(goto done);
			if ( !match->decode(match, identity) )
				ERR(goto done);
			retn = true;
			goto done;
		}

		++ivyp;
		idf->reset(idf);
	}


 done:
	WHACK(idkey);
	WHACK(identity);
	WHACK(idf);

	return retn;
}


/**
 * Private function.
 *
 * This function is responsible for searching the list of attestable
 * client identities based on the identification challenge.
 *
 * \param packet	The Buffer object containing the type 1 POSSUM
 *			packet which was received.
 *
 * \param id		A pointer to the object that will be loaded
 *			with the identity key of the client.
 *
 * \return		A true value is used to indicate the search
 *			for the client was successful.  A false value
 *			is returned if the search was unsuccessful.
 */

static _Bool find_client2(CO(Buffer, packet), RSAkey *id)

{
	_Bool retn = false;

	size_t lp,
	       cnt;

	Buffer bufr	= NULL,
	       idkey	= NULL,
	       identity = NULL;

	RSAkey *keyp,
		key;

	SHA256_hmac idf = NULL;


	/* Verify packet status. */
	if ( packet->poisoned(packet) )
		ERR(goto done);
	if ( Verifiers == NULL )
		ERR(goto done);
	if ( Verifiers->size(Verifiers) == 0 )
		ERR(goto done);


	/* Initialize objects to be used. */
	INIT(HurdLib, Buffer, idkey, ERR(goto done));
	INIT(HurdLib, Buffer, identity, ERR(goto done));


	/* Load the identity key/salt and the asserted client identity. */
	if ( !idkey->add(idkey, packet->get(packet), NAAAIM_IDSIZE) )
		ERR(goto done);

	if ( !identity->add(identity, packet->get(packet) + NAAAIM_IDSIZE, \
			    NAAAIM_IDSIZE) )
		ERR(goto done);
	if ( (idf = NAAAIM_SHA256_hmac_Init(idkey)) == NULL )
		ERR(goto done);


	/*
	 * Loop over the list of counter-party identities and determine
	 * which of the identities match the identity assertion issued
	 * by the POSSUM counter-party.
	 */
	INIT(HurdLib, Buffer, bufr, ERR(goto done));

	keyp = (RSAkey *) Verifiers->get(Verifiers);
	cnt  = Verifiers->size(Verifiers) / sizeof(RSAkey);

	for (lp= 0; lp < cnt; ++lp) {
		key = *keyp;

		if ( !key->get_modulus(key, bufr) )
			ERR(goto done);
		idf->add_Buffer(idf, bufr);
		if ( !idf->compute(idf) )
			ERR(goto done);

		if ( identity->equal(identity, idf->get_Buffer(idf)) ) {
			*id = *keyp;
			retn = true;
			goto done;
		}

		++keyp;
		idf->reset(idf);
		bufr->reset(bufr);
	}


 done:
	WHACK(bufr);
	WHACK(idkey);
	WHACK(identity);
	WHACK(idf);

	return retn;
}

#if 0
/**
 * Private function.
 *
 * This function is responsible for setting the active personality in
 * the configuration object to the identity of the counter-party
 * which is requesting setup of the session.
 *
 * \param config	The configuration object which is to have its
 *			personality configured.
 *
 * \param token	        The identity of the counter-party involved in
 *			the connection.  The organizational ID of this
 *			counter-party is used as the personality name
 *			of the section.
 *
 * \return		A false value indicates the personality could
 *			not be set or did not exist.  A true value
 *			indicates the personality was successfuly
 *			set.
 */

static _Bool set_counter_party_personality(CO(Config, cfg), CO(IDtoken, token))

{
	_Bool retn = false;

	unsigned char *p;

	char bufr[3];

	size_t lp,
	       cnt;

	Buffer b;

	String personality = NULL;


	INIT(HurdLib, String, personality, goto done);
	if ( (b = token->get_element(token, IDtoken_id)) == NULL )
		ERR(goto done);

	p   = b->get(b);
        cnt = b->size(b);
        for (lp= 0; lp < cnt; ++lp) {
                snprintf(bufr, sizeof(bufr), "%02x", *p);
                personality->add(personality, bufr);
                ++p;
        }

	if ( cfg->set_section(cfg, personality->get(personality)) )
		retn = true;

 done:
	WHACK(personality);

	return retn;
}
#endif


/**
 * Private method.
 *
 * This function is responsible for generating the shared keys which
 * will be used to generate to generate.  Two separate schedules are
 * generated.
 *
 * The first is based on the following:
 *
 *	Shared1 = sha256(HMAC_dhkey(client_nonce ^ host_nonce))
 *
 *	Shared2 = sha256(HMAC_dhkey(host_nonce || client_nonce))
 *
 *	Where dhkey is the shared secret generated from the Diffie-Hellman
 *	key exchange.
 *
 * The two shared keys are stored in the object state for subsequent
 * generation of the encryption and authentication keys.
 *
 * \param this		The object whose shared keys are to be generated.

 * \param nonce1	The first nonce to be used in key generation.
 *
 * \param nonce2	The second nonce to be used in key generation.
 *
 * \param dhkey		The Diffie-Hellman key to be used in computing
 *			the shared secret.

 * \param public	The public key to be used in combination with
 *			the public key in the dhkey parameter to generate
 *			shared secret.
 *
 * \return		A true value is used to indicate the keys were
 *			successfully generated.  A false value indicates
 *			that key generation failed.
 */

static _Bool generate_shared_keys(CO(PossumPipe, this), CO(Buffer, nonce1),   \
				  CO(Buffer, nonce2),  CO(Curve25519, dhkey), \
				  CO(Buffer, public))

{
	STATE(S);

	_Bool retn = false;

	unsigned char *p,
		      *p1;

	unsigned int lp;

	Buffer xor = NULL,
	       key = NULL;

	Sha256 sha256 = NULL;

	SHA256_hmac hmac;


	/* Compute the shared secret and initialize the HMAC. */
	INIT(HurdLib, Buffer, key, goto done);
	dhkey->compute(dhkey, public, key);

	if ( (hmac = NAAAIM_SHA256_hmac_Init(key)) == NULL )
		ERR(goto done);

	/*
	 * Generate the HMACsha256 hash of the inverted concantenated
	 * nonces and then hash the output so it is ready for use.
	 */
	hmac->add_Buffer(hmac, nonce2);
	hmac->add_Buffer(hmac, nonce1);
	if ( !hmac->compute(hmac) )
		ERR(goto done);

	S->shared1->add(S->shared1, hmac->get_Buffer(hmac));
	if ( !S->shared1->compute(S->shared1) )
		ERR(goto done);


	/* XOR the supplied nonces. */
	INIT(HurdLib, Buffer, xor, goto done);
	if ( nonce1->size(nonce1) != nonce2->size(nonce2) )
		ERR(goto done);
	if ( !xor->add_Buffer(xor, nonce1) )
		ERR(goto done);

	p  = xor->get(xor);
	p1 = nonce2->get(nonce2);
	for (lp= 0; lp < xor->size(xor); ++lp) {
		*p ^= *p1;
		++p;
		++p1;
	}

	/*
	 * Generate the HMACsha256 hash of the XOR'ed buffer under
	 * the ECDH generated key.
	 */
	hmac->reset(hmac);
	hmac->add_Buffer(hmac, xor);
	if ( !hmac->compute(hmac) )
		ERR(goto done);

	S->shared2->add(S->shared2, hmac->get_Buffer(hmac));
	if ( !S->shared2->compute(S->shared2) )
		ERR(goto done);

	retn = true;


 done:
	WHACK(xor);
	WHACK(key);
	WHACK(sha256);
	WHACK(hmac);

	return retn;
}


/**
 * Private function.
 *
 * This is a helper function specific to the SGX implementation of
 * the protocol.  This function extracts the nonce specific software
 * measurement that is to be used in the protocol.
 *
 * \param measurement	The object which will be populated with the
 *			software measurement to be used.
 *
 * \param nonce		The object containing the nonce being used
 *			with the counter-party.
 *
 * \param quote		A pointer to the quote structure generated by
 *			the communication counter-party.
 *
 * \return		If an error is encountered extracting the
 *			report data a false value is returned.  If the
 *			software status is updated a true value is
 *			returned.
 */

static _Bool _update_measurement(CO(Buffer, measurement), CO(Buffer, nonce), \
				 struct SRDE_quote *quote)

{
	_Bool retn = false;

	uint8_t *p,
		*p1;

	unsigned int lp;

	Buffer b;

	Sha256 sha256 = NULL;


	/* Generate the XOR shroud value and convert the report. */
	INIT(NAAAIM, Sha256, sha256, ERR(goto done));

	if ( !sha256->add(sha256, nonce) )
		ERR(goto done);

	sha256->compute(sha256);
	if ( !sha256->rehash(sha256, 99) )
		ERR(goto done);
	b = sha256->get_Buffer(sha256);

	p  = quote->report_body.reportdata;
	p1 = b->get(b);

	for (lp= 0; lp < 16; ++lp)
		*p++ ^= *p1++;


	/* Extract the software measurement. */
	measurement->reset(measurement);
	if ( !measurement->add(measurement, quote->report_body.reportdata, \
			       32) )
		ERR(goto done);
	retn = true;


 done:
	WHACK(sha256);

	return retn;
}


/**
 * Private method.
 *
 * This method receives and authenticates the hardware reference
 * quote.
 *
 * \param this		The object which is to receive the hardware
 *			reference quote.
 *
 * \param bufr		The Buffer object which is to be used to receive
 *			the platform quote.
 *
 * \param ivy		The identify reference which is to be used to
 *			verify the platform reference quote.
 *
 * \param nonce		The nonce which was used to create the reference.
 *
 * \return		If the quote is received and verified a true
 *			value is returned.  If an error is encountered a
 *			false value is returned.
 */

static _Bool receive_platform_quote(CO(PossumPipe, this), CO(Buffer, bufr), \
				    CO(Ivy, ivy), CO(Buffer, nonce))

{
	STATE(S);

	_Bool retn = false;

	size_t payload;

	Buffer key   = S->shared2->get_Buffer(S->shared2),
	       cksum = NULL,
	       iv    = NULL,
	       quote = NULL;

#if 0
	TPMcmd tpmcmd = NULL;
#else
	String output = NULL;
#endif

	AES256_cbc cipher = NULL;

	SHA256_hmac hmac = NULL;


	if ( !S->duct->receive_Buffer(S->duct, bufr) ) {
		if ( S->duct->eof(S->duct) ) {
			S->error = PossumPipe_error_closed_pipe;
			goto done;
		}
		ERR(goto done);
	}
	if ( bufr->size(bufr) == 0 ) {
		S->error = PossumPipe_error_closed_pipe;
		goto done;
	}

	INIT(HurdLib, Buffer, cksum, goto done);
	payload = bufr->size(bufr) - 32;
	cksum->add(cksum, bufr->get(bufr) + payload, 32);


	if ( (hmac = NAAAIM_SHA256_hmac_Init(key)) == NULL )
		ERR(goto done);
	bufr->shrink(bufr, 32);
	hmac->add_Buffer(hmac, bufr);
	if ( !hmac->compute(hmac) )
		ERR(goto done);
	if ( !cksum->equal(cksum, hmac->get_Buffer(hmac)) )
		ERR(goto done);


	INIT(HurdLib, Buffer, iv, goto done);
	if ( !iv->add(iv, bufr->get(bufr), 16) )
		ERR(goto done);

	if ( (cipher = NAAAIM_AES256_cbc_Init_decrypt(key, iv)) \
	     == NULL )
		ERR(goto done);

	INIT(HurdLib, Buffer, quote, goto done);
	cksum->reset(cksum);
	if ( !cksum->add(cksum, bufr->get(bufr) + 16, bufr->size(bufr) - 16) )
		ERR(goto done);
	if ( !cipher->decrypt(cipher, cksum) )
		ERR(goto done);
	if ( !quote->add_Buffer(quote, cipher->get_Buffer(cipher)) )
		ERR(goto done);

	if ( S->debug ) {
		fputs("\nClient platform quote:\n", stdout);
		quote->hprint(quote);
	}


#if 0
	INIT(NAAAIM, TPMcmd, tpmcmd, goto done);

	if ( (pubkey = ivy->get_element(ivy, Ivy_pubkey)) == NULL )
		ERR(goto done);
	if ( (ref = ivy->get_element(ivy, Ivy_reference)) == NULL )
		ERR(goto done);
	if ( !tpmcmd->verify(tpmcmd, pubkey, ref, nonce, quote) )
		ERR(goto done);
#else
	INIT(HurdLib, String, output, ERR(goto done));

	INIT(NAAAIM, SRDEquote, S->remote, ERR(goto done));
	if ( !S->remote->generate_report(S->remote, quote, output, NULL) )
		ERR(goto done);

	if ( !S->remote->decode_report(S->remote, output) )
		ERR(goto done);
	if ( S->debug ) {
		fputs("\nAttestation report:\n", stdout);
		S->remote->dump_report(S->remote);
	}

	if ( !_update_measurement(S->software, nonce, \
				  S->remote->get_quoteinfo(S->remote)) )
		ERR(goto done);
#endif
	retn = true;


 done:
	WHACK(cksum);
	WHACK(iv);
	WHACK(quote);
#if 0
	WHACK(tpmcmd);
#else
	WHACK(output);
#endif
	WHACK(cipher);
	WHACK(hmac);

	return retn;
}


/**
 * Private method.
 *
 * This method receives and authenticates an enclave attestation report.
 *
 * \param this		The object which is to receive the hardware
 *			reference quote.
 *
 * \param bufr		The Buffer object which is to be used to receive
 *			the platform quote.
 *
 * \param nonce		The nonce that was used to create the reference.
 *
 * \return		A false value is returned if an error is
 *			encountered in receiving or verifying the
 *			attestation report.  A true value is returned
 *			if the report was successfully received and
 *			verified.
 */

static _Bool receive_platform_report(CO(PossumPipe, this), CO(Buffer, bufr), \
				     CO(Buffer, nonce))

{
	STATE(S);

	_Bool report_valid,
	      retn = false;

	size_t payload;

	Buffer key   = S->shared2->get_Buffer(S->shared2),
	       cksum = NULL,
	       iv    = NULL,
	       quote = NULL;

	String output = NULL;

	AES256_cbc cipher = NULL;

	SHA256_hmac hmac = NULL;


	if ( !S->duct->receive_Buffer(S->duct, bufr) ) {
		if ( S->duct->eof(S->duct) ) {
			S->error = PossumPipe_error_closed_pipe;
			goto done;
		}
		ERR(goto done);
	}
	if ( bufr->size(bufr) == 0 ) {
		S->error = PossumPipe_error_closed_pipe;
		goto done;
	}

	INIT(HurdLib, Buffer, cksum, goto done);
	payload = bufr->size(bufr) - 32;
	cksum->add(cksum, bufr->get(bufr) + payload, 32);


	if ( (hmac = NAAAIM_SHA256_hmac_Init(key)) == NULL )
		ERR(goto done);
	bufr->shrink(bufr, 32);
	hmac->add_Buffer(hmac, bufr);
	if ( !hmac->compute(hmac) )
		ERR(goto done);
	if ( !cksum->equal(cksum, hmac->get_Buffer(hmac)) )
		ERR(goto done);


	INIT(HurdLib, Buffer, iv, goto done);
	if ( !iv->add(iv, bufr->get(bufr), 16) )
		ERR(goto done);

	if ( (cipher = NAAAIM_AES256_cbc_Init_decrypt(key, iv)) \
	     == NULL )
		ERR(goto done);

	INIT(HurdLib, Buffer, quote, goto done);
	cksum->reset(cksum);
	if ( !cksum->add(cksum, bufr->get(bufr) + 16, bufr->size(bufr) - 16) )
		ERR(goto done);
	if ( !cipher->decrypt(cipher, cksum) )
		ERR(goto done);
	if ( !quote->add_Buffer(quote, cipher->get_Buffer(cipher)) )
		ERR(goto done);

	if ( S->debug ) {
		fputs("\nClient platform quote:\n", stdout);
		quote->hprint(quote);
	}

	INIT(HurdLib, String, output, ERR(goto done));
	if ( !output->add(output, (char *) quote->get(quote)) )
		ERR(goto done);

	INIT(NAAAIM, SRDEquote, S->remote, ERR(goto done));
	if ( !S->remote->decode_report(S->remote, output) )
		ERR(goto done);
	if ( !S->remote->validate_report(S->remote, &report_valid) )
		ERR(goto done);
	if ( !report_valid )
		ERR(goto done);

	if ( S->debug ) {
		fputs("\nAttestation report:\n", stdout);
		S->remote->dump_report(S->remote);
	}

	if ( !_update_measurement(S->software, nonce, \
				  S->remote->get_quoteinfo(S->remote)) )
		ERR(goto done);
	retn = true;


 done:
	WHACK(cksum);
	WHACK(iv);
	WHACK(quote);
	WHACK(output);
	WHACK(cipher);
	WHACK(hmac);

	return retn;
}


/**
 * Internal private function.
 *
 * This function is a helper function for the send_platform_quote
 * function.  It encapsulates the functionality needed to generate a
 * platform quote.
 *
 * \param nonce		The object containing the nonce that was
 *			supplied by the counter-party in the
 *			initiation message.
 *
 * \param spid		The object containing the spid value that
 *			was supplied by the counter-party to generate
 *			the quote against.
 *
 * \param quote		The object which the generated quote will
 *			be placed in.
 *
 * \return		If the quote is received and verified a true
 *			value is returned.  If an error is encountered a
 *			false value is returned.
 */

static _Bool _generate_quote(CO(Buffer, nonce), CO(Buffer, spid), \
			     CO(Buffer, quote))

{
	_Bool retn = false;

	char report_data[64] __attribute__((aligned(128)));

	uint8_t *p,
		*p1,
		keydata[16] __attribute__((aligned(128)));

	unsigned int lp;

	struct SGX_report __attribute__((aligned(512))) report;

	struct SGX_targetinfo *tp,
			      target;

	Buffer b,
	       qnonce = NULL;

	SoftwareStatus software = NULL;

	SRDEquote quoter = NULL;

	Sha256 sha256 = NULL;


	/* Initialize the quoting object. */
	INIT(NAAAIM, SRDEquote, quoter, ERR(goto done));
	if ( !quoter->init(quoter, NULL, NULL, NULL) )
		ERR(goto done);
	tp = quoter->get_qe_targetinfo(quoter);
	target = *tp;

	/* Derive a key to be used for the XOR shroud of the report data. */
	INIT(NAAAIM, Sha256, sha256, ERR(goto done));

	if ( !sha256->add(sha256, nonce) )
		ERR(goto done);

	sha256->compute(sha256);
	if ( !sha256->rehash(sha256, 99) )
		ERR(goto done);

	/* Request a nonce derived enclave measurement. */
	INIT(NAAAIM, SoftwareStatus, software, goto done);
	if ( !software->open(software) )
		ERR(goto done);
	if ( !software->measure_derived(software, nonce->get(nonce)) )
		ERR(goto done);
	b = software->get_template_hash(software);

	memset(report_data, '\0', sizeof(report_data));
	memcpy(report_data, b->get(b), b->size(b));

	p  = (uint8_t *) report_data;
	b  = sha256->get_Buffer(sha256);
	p1 = b->get(b);
	for (lp= 0; lp < sizeof(keydata); ++lp)
		*p++ ^= *p1++;

	/* Generate the report for this enclave. */
	memset(&report, '\0', sizeof(struct SGX_report));
	enclu_ereport(&target, &report, report_data);

	INIT(HurdLib, Buffer, qnonce, ERR(goto done));
	if ( !qnonce->add(qnonce, nonce->get(nonce), 16) )
		ERR(goto done);

	/* Request the quote. */
	if ( !quoter->generate_quote(quoter, &report, spid, qnonce, quote) )
		ERR(goto done);
	retn = true;


 done:
	WHACK(software);
	WHACK(qnonce);
	WHACK(quoter);
	WHACK(sha256);

	return retn;
}


/**
 * Private method.
 *
 * This method creates and sends a platform reference quote.
 *
 * \param this		The object which is to send the reference quote.
 *
 * \param bufr		The Buffer object which is to be used to transmit
 *			the platform quote.
 *
 * \param key		The shared key to be used to encrypt the reference
 *			quote.
 *
 * \param nonce		The nonce to be used to generate the quote.
 *
 * \param spid		The object containing the service provider
 *			identity that is to be used to generate the
 *			quote.
 *
 * \return		If the quote is received and verified a true
 *			value is returned.  If an error is encountered a
 *			false value is returned.
 */

static _Bool send_platform_quote(CO(PossumPipe, this), CO(Buffer, bufr), \
				 CO(Buffer, nonce), CO(Buffer, spid))

{
	STATE(S);

	_Bool retn = false;

	Duct duct = S->duct;

	Buffer b,
	       key   = S->shared2->get_Buffer(S->shared2),
	       uuid  = NULL,
	       quote = NULL;

#if 0
	File aik_file = NULL;

	TPMcmd tpmcmd = NULL;
#endif

	AES256_cbc cipher = NULL;

	RandomBuffer iv = NULL;

	SHA256_hmac hmac = NULL;


#if 0
	INIT(HurdLib, File, aik_file, goto done);
	INIT(HurdLib, Buffer, uuid, goto done);
	aik_file->open_ro(aik_file, "/etc/conf/aik");
	if ( !aik_file->slurp(aik_file, uuid) )
		ERR(goto done);
#endif

	INIT(HurdLib, Buffer, quote, goto done);
	INIT(NAAAIM, RandomBuffer, iv, goto done);

#if 0
	INIT(NAAAIM, TPMcmd, tpmcmd, goto done);
	if ( !tpmcmd->pcrmask(tpmcmd, 10, 15, 17, 18, -1) )
		ERR(goto done);
	if ( !quote->add_Buffer(quote, nonce) )
		ERR(goto done);

	if ( !tpmcmd->quote(tpmcmd, uuid, quote) )
		ERR(goto done);
#else
	if ( !_generate_quote(nonce, spid, quote) )
		ERR(goto done);
	memcpy(S->software_nonce, nonce->get(nonce), \
	       sizeof(S->software_nonce));
#endif

	if ( !iv->generate(iv, 16) )
		ERR(goto done);
	b = iv->get_Buffer(iv);
	bufr->add_Buffer(bufr, b);

	if ( (cipher = NAAAIM_AES256_cbc_Init_encrypt(key, b)) == NULL )
		ERR(goto done);
	if ( !cipher->encrypt(cipher, quote) )
		ERR(goto done);
	if ( !bufr->add_Buffer(bufr, cipher->get_Buffer(cipher)) )
		ERR(goto done);

	if ( (hmac = NAAAIM_SHA256_hmac_Init(key)) == NULL )
		ERR(goto done);
	hmac->add_Buffer(hmac, bufr);
	if ( !hmac->compute(hmac) )
		ERR(goto done);
	if ( !bufr->add_Buffer(bufr, hmac->get_Buffer(hmac)) )
		ERR(goto done);

	if ( !duct->send_Buffer(duct, bufr) )
		ERR(goto done);
	retn = true;


 done:
	WHACK(uuid);
	WHACK(quote);
#if 0
	WHACK(aik_file);
	WHACK(tpmcmd);
#endif
	WHACK(iv);
	WHACK(cipher);
	WHACK(hmac);

	return retn;
}


/**
 * Private method.
 *
 * This method creates and sends an enclave attestation report.  It is
 * designed for an environment where the communication's counter-party
 * is not in a position to query Intel IAS for a report on a given
 * quote.  The security predicate is based on the notion that the
 * relying party can verify that the report is valid based on the
 * Intel signature over the report.
 *
 * \param this		The object which is to send the reference quote.
 *
 * \param bufr		The Buffer object which is to be used to transmit
 *			the platform quote.
 *
 * \param key		The shared key to be used to encrypt the reference
 *			quote.
 *
 * \param nonce		The nonce to be used to generate the quote.
 *
 * \param spid		The object containing the service provider
 *			identity that is to be used to generate the
 *			quote.
 *
 * \return		If an error is encountered during report generation
 *			or transmission a value value is returned.  A
 *			true value is returned if the report is
 *			successfully generated and sent.
 */

static _Bool send_platform_report(CO(PossumPipe, this), CO(Buffer, bufr), \
				  CO(Buffer, nonce), CO(Buffer, spid))

{
	STATE(S);

	_Bool retn = false;

	Duct duct = S->duct;

	Buffer b,
	       key   = S->shared2->get_Buffer(S->shared2),
	       quote = NULL;

	String rpt = NULL;

	AES256_cbc cipher = NULL;

	RandomBuffer iv = NULL;

	SHA256_hmac hmac = NULL;

	SRDEquote report = NULL;


	/* Generate the platform quote and report. */
	INIT(HurdLib, Buffer, quote, ERR(goto done));
	INIT(NAAAIM, RandomBuffer, iv, ERR(goto done));

	if ( !_generate_quote(nonce, spid, quote) )
		ERR(goto done);
	memcpy(S->software_nonce, nonce->get(nonce), \
	       sizeof(S->software_nonce));

	INIT(NAAAIM, SRDEquote, report, ERR(goto done));
	INIT(HurdLib, String, rpt, ERR(goto done));
	if ( !report->generate_report(report, quote, rpt, NULL) )
		ERR(goto done);

	quote->reset(quote);
	if ( !quote->add(quote, (unsigned char *) rpt->get(rpt), \
			 rpt->size(rpt) + 1) )
		ERR(goto done);

	if ( !iv->generate(iv, 16) )
		ERR(goto done);
	b = iv->get_Buffer(iv);
	bufr->add_Buffer(bufr, b);

	if ( (cipher = NAAAIM_AES256_cbc_Init_encrypt(key, b)) == NULL )
		ERR(goto done);
	if ( !cipher->encrypt(cipher, quote) )
		ERR(goto done);
	if ( !bufr->add_Buffer(bufr, cipher->get_Buffer(cipher)) )
		ERR(goto done);

	if ( (hmac = NAAAIM_SHA256_hmac_Init(key)) == NULL )
		ERR(goto done);
	hmac->add_Buffer(hmac, bufr);
	if ( !hmac->compute(hmac) )
		ERR(goto done);
	if ( !bufr->add_Buffer(bufr, hmac->get_Buffer(hmac)) )
		ERR(goto done);

	if ( !duct->send_Buffer(duct, bufr) )
		ERR(goto done);
	retn = true;


 done:
	WHACK(quote);
	WHACK(rpt);
	WHACK(cipher);
	WHACK(iv);
	WHACK(hmac);
	WHACK(report);

	return retn;
}


/**
 * Private method.
 *
 * This function receives and confirms a request to initiate a
 * connection from the client.
 *
 * \param this	The object which is to receive the connection start.
 *
 * \param bufr	The Buffer object which is to be used to receive
 *		the initiation request.
 *
 * \param key	The shared key used to authenticated the request.
 *
 * \return	If a confirmation quote is received without
 *		error and validated a true value is returned.  A
 *		false value indicates the confirmation failed.
 */

static _Bool receive_connection_start(CO(PossumPipe, this), CO(Buffer, bufr))

{
	STATE(S);

	_Bool retn = false;

	size_t payload;

	Duct duct = S->duct;

	Buffer b,
	       key   = S->shared2->get_Buffer(S->shared2),
	       cksum = NULL,
	       iv    = NULL;

	AES256_cbc cipher = NULL;

	Sha256 sha256 = NULL;

	SHA256_hmac hmac = NULL;


	/* Receive the confirmation message. */
	if ( !duct->receive_Buffer(duct, bufr) )
		ERR(goto done);


	/* Validate the confirmation checksum. */
	INIT(HurdLib, Buffer, cksum, goto done);
	payload = bufr->size(bufr) - 32;
	cksum->add(cksum, bufr->get(bufr) + payload, 32);

	if ( (hmac = NAAAIM_SHA256_hmac_Init(key)) == NULL )
		ERR(goto done);
	bufr->shrink(bufr, 32);
	hmac->add_Buffer(hmac, bufr);
	if ( !hmac->compute(hmac) )
		ERR(goto done);
	if ( !cksum->equal(cksum, hmac->get_Buffer(hmac)) )
		ERR(goto done);


	/* Decrypt the authenticator. */
	INIT(HurdLib, Buffer, iv, goto done);
	if ( !iv->add(iv, bufr->get(bufr), 16) )
		ERR(goto done);

	if ( (cipher = NAAAIM_AES256_cbc_Init_decrypt(key, iv)) == NULL )
		ERR(goto done);
	cksum->reset(cksum);
	if ( !cksum->add(cksum, bufr->get(bufr) + 16, bufr->size(bufr) - 16) )
		ERR(goto done);
	if ( !cipher->decrypt(cipher, cksum) )
		ERR(goto done);


	/* Confirm the authenticator. */
	INIT(NAAAIM, Sha256, sha256, goto done);
	sha256->add(sha256, key);
	if ( !sha256->compute(sha256) )
		ERR(goto done);
	b = sha256->get_Buffer(sha256);
	if ( !b->equal(b, cipher->get_Buffer(cipher)) )
		ERR(goto done);

	retn = true;


 done:
	WHACK(cksum);
	WHACK(iv);
	WHACK(cipher);
	WHACK(sha256);
	WHACK(hmac);

	return retn;

}


/**
 * External public method.
 *
 * This method implements handling the authentication and initiation of
 * a client connection.  It is designed to be called after the successful
 * acceptance of a client connection.
 *
 * \param this		A pointer to the object which is to be initiated
 *			in server mode.
 *
 * \param spid		The object containing the service provider
 *			identity that is to be used to authenticate the
 *			client attestation quote.
 *
 * \return		A boolean value is returned to indicate the
 *			status of session initiation.  A false value
 *			indicates that connection setup failed while
 *			a true value indicates a session has been
 *			established and is valid.
 */

static _Bool start_host_mode(CO(PossumPipe, this), CO(Buffer, spid))

{
	STATE(S);

	_Bool retn = false;

	uint32_t spi = 0xffff;

	SoftwareStatus software_status = NULL;

	Duct duct = S->duct;

	Buffer b,
	       our_nonce,
	       netbufr		= NULL,
	       nonce		= NULL,
	       quote_nonce	= NULL,
	       public		= NULL;

	String name	 = NULL,
	       remote_ip = NULL;

	IDtoken token  = NULL;

	PossumPacket packet = NULL;

	Curve25519 dhkey = NULL;

	IDmgr idmgr = NULL;

	Ivy ivy = NULL;


	/* Get current software status. */
	INIT(NAAAIM, SoftwareStatus, software_status, goto done);
	software_status->open(software_status);
	if ( !software_status->measure(software_status) )
		ERR(goto done);

	b = software_status->get_template_hash(software_status);
	if ( S->debug ) {
		fprintf(stdout, "\n%s: Local software status:\n", __func__, \
			stdout);
		b->print(b);
	}

	/* Setup the network port. */
	INIT(HurdLib, Buffer, netbufr, goto done);

	/* Wait for a packet to arrive. */
	if ( S->debug )
		fprintf(stderr, "\n%s: Waiting for initialization packet.\n", \
			__func__);
	if ( !duct->receive_Buffer(duct, netbufr) )
		ERR(goto done);

	if ( S->debug ) {
		fprintf(stdout, "\n%s: Received client initialization " \
			"packet:\n", __func__);
		netbufr->hprint(netbufr);
	}

	/* Lookup the client identity. */
	INIT(NAAAIM, IDtoken, token, goto done);
	INIT(NAAAIM, Ivy, ivy, goto done);
	if ( !find_client(netbufr, token, ivy) ) {
		S->error = PossumPipe_error_no_identity;
		goto done;
	}

	if ( (b = ivy->get_element(ivy, Ivy_software)) == NULL )
		ERR(goto done);
	S->software->add_Buffer(S->software, b);

	if ( S->debug ) {
		fprintf(stdout, "\n%s: Using client software status:\n", \
			__func__, stdout);
		S->software->print(S->software);
	}


#if 0
	/*
	 * Setting the counter party personality was only needed when
	 * the PossumPipe object was intimately connected to establishing
	 * an IPsec tunnel.
	 *
	 * A decision needs to be made as to how the remote client
	 * is to be surfaced from this object since the remote client
	 * is a characteristic of the connection.
	 */
	if ( !set_counter_party_personality(cfg, token) ) {
		fputs("Cannot find personality.\n", stdout);
		ERR(goto done);
	}
#endif

	/* Verify and decode packet. */
	INIT(NAAAIM, PossumPacket, packet, goto done);
	if ( !packet->decode_packet1(packet, token, S->software, netbufr) ) {
		S->error = PossumPipe_error_invalid_identity;
		goto done;
	}

	if ( S->debug ) {
		fprintf(stdout, "\n%s: Incoming client packet 1:\n", __func__);
		packet->print(packet);
	}

	/* Extract the replay and quote nonces supplied by client. */
	INIT(HurdLib, Buffer, nonce, goto done);
	INIT(HurdLib, Buffer, quote_nonce, goto done);
	if ( (b = packet->get_element(packet, PossumPacket_replay_nonce)) \
	     == NULL )
		ERR(goto done);
	if ( !nonce->add_Buffer(nonce, b) )
		ERR(goto done);
	if ( (b = packet->get_element(packet, PossumPacket_quote_nonce)) \
	     == NULL )
		ERR(goto done);

	if ( !quote_nonce->add_Buffer(quote_nonce, b) )
		ERR(goto done);

	/* Verify hardware quote. */

	/* Verify protocol. */

	INIT(HurdLib, Buffer, public, goto done);
	if ( (b = packet->get_element(packet, PossumPacket_public)) == NULL )
		ERR(goto done);
	if ( !public->add_Buffer(public, b) )
		ERR(goto done);

	/* Generate DH public key for shared secret. */
	INIT(NAAAIM, Curve25519, dhkey, goto done);
	dhkey->generate(dhkey);

	/* Compose and send a reply packet. */
	token->reset(token);
	INIT(NAAAIM, IDmgr, idmgr, goto done);
	if ( (name = HurdLib_String_Init_cstr("device")) == NULL )
		ERR(goto done);

	idmgr->attach(idmgr);
	if ( !idmgr->get_idtoken(idmgr, name, token) )
		ERR(goto done);

	packet->reset(packet);
	netbufr->reset(netbufr);

	/*
	 * Check both SPI's proposed by the caller.  If neither are
	 * available allocate an SPI from the reserve arena.
	 */
#if 0
	if ( (tspi = packet->get_value(packet, PossumPacket_spi)) == 0 )
		ERR(goto done);
	spi = propose_spi(HOST_SPI_ARENA, tspi & UINT16_MAX);
	if ( spi == 0 )
		spi = propose_spi(CLIENT_SPI_ARENA, tspi >> 16);
	if ( spi == 0 )
		spi = propose_spi(RESERVE_SPI_ARENA, 0);
	if ( spi == 0 )
		ERR(goto done);
#endif

	packet->set_schedule(packet, token, time(NULL));
	packet->create_packet1(packet, token, dhkey, spi, spid);

	/* Reset the section back to the original. */
	b = software_status->get_template_hash(software_status);
	if ( !packet->encode_packet1(packet, b, netbufr) )
		ERR(goto done);

	if ( S->debug ) {
		fprintf(stdout, "\n%s: Sending host packet 1:\n", __func__);
		packet->print(packet);
	}

	if ( !duct->send_Buffer(duct, netbufr) )
		ERR(goto done);

	if ( (our_nonce = packet->get_element(packet,			  \
					      PossumPacket_replay_nonce)) \
	     == NULL )
		ERR(goto done);
	if ( !generate_shared_keys(this, our_nonce, nonce, dhkey, public) ) {
		fputs("Failed key generation.\n", stderr);
		ERR(goto done);
	}

	/* Wait for platform verification reference quote. */
	netbufr->reset(netbufr);
	if ( (b = packet->get_element(packet, PossumPacket_quote_nonce)) == \
	     NULL )
		ERR(goto done);
	if ( !receive_platform_quote(this, netbufr, ivy, nonce) ) {
		if ( S->error == PossumPipe_error_internal )
			ERR(goto done);
		goto done;
	}

	/* Send platform verification quote. */
	netbufr->reset(netbufr);
	if ( !send_platform_quote(this, netbufr, our_nonce, quote_nonce) )
		ERR(goto done);

	/* Get connection confirmation start. */
	netbufr->reset(netbufr);
	if ( !receive_connection_start(this, netbufr) ) {
		fputs("Failed connection start.\n", stderr);
		ERR(goto done);
	}

	if ( S->debug ) {
		fputs("\nShared 1:\n", stderr);
		S->shared1->print(S->shared1);
		fputs("Shared 2:\n", stderr);
		S->shared2->print(S->shared2);
	}

	S->sent->add(S->sent, S->shared1->get_Buffer(S->shared1));
	S->sent->add(S->sent, S->shared2->get_Buffer(S->shared2));
	if ( !S->sent->compute(S->sent) )
		ERR(goto done);
	if ( S->debug ) {
		fputs("\nTransmit root:\n", stderr);
		S->sent->print(S->sent);
	}

	S->received->add(S->received, S->shared2->get_Buffer(S->shared2));
	S->received->add(S->received, S->shared1->get_Buffer(S->shared1));
	if ( !S->received->compute(S->received) )
		ERR(goto done);
	if ( S->debug ) {
		fputs("Receive root:\n", stderr);
		S->received->print(S->received);
	}


	/* Setup host nonce generator. */
	if ( !_setup_nonce(S) )
		ERR(goto done);

	retn = true;


 done:
	WHACK(software_status);
	WHACK(netbufr);
	WHACK(nonce);
	WHACK(quote_nonce);
	WHACK(public);
	WHACK(token);
	WHACK(packet);
	WHACK(dhkey);
	WHACK(name);
	WHACK(remote_ip);
	WHACK(idmgr);
	WHACK(ivy);

	return retn;
}


/**
 * External public method.
 *
 * This method implements handling the authentication and initiation of
 * a client connection using mode 2 authentication.  The mode 2
 * authentication strategy uses the public modulus of a provisioned
 * private/public keypair as the platform identifier.  It is designed
 * primarily as a bootstrap enabling method for establishing standard
 * host identity based credentials on a platform.
 *
 * \param this		A pointer to the object which is to be initiated
 *			in server mode.
 *
 * \param spid		The object containing the service provider
 *			identity that that will be used to generate
 *			platform reports for both the host and client.
 *
 * \return		A boolean value is returned to indicate the
 *			status of session initiation.  A false value
 *			indicates that connection setup failed while
 *			a true value indicates a session has been
 *			established and is valid.
 */

static _Bool start_host_mode2(CO(PossumPipe, this), CO(Buffer, spid))

{
	STATE(S);

	_Bool retn = false;

	Duct duct = S->duct;

	Buffer b,
	       our_nonce,
	       netbufr		= NULL,
	       nonce		= NULL,
	       quote_nonce	= NULL,
	       public		= NULL;

	String name	 = NULL,
	       remote_ip = NULL;

	PossumPacket packet = NULL;

	Curve25519 dhkey = NULL;

	SHA256_hmac hmac = NULL;

	RSAkey identity;


	/* Configure default mode2 software status. */
	if ( !S->software->add(S->software, mode2_status, \
			       sizeof(mode2_status)) )
		ERR(goto done);

	/* Setup the network port. */
	INIT(HurdLib, Buffer, netbufr, goto done);

	/* Wait for a packet to arrive. */
	if ( S->debug )
		fprintf(stderr, "\n%s: Waiting for initialization packet.\n", \
			__func__);
	if ( !duct->receive_Buffer(duct, netbufr) )
		ERR(goto done);

	if ( S->debug ) {
		fprintf(stdout, "\n%s: Received client initialization " \
			"packet:\n", __func__);
		netbufr->hprint(netbufr);
	}

	/* Lookup the client identity. */
	if ( !find_client2(netbufr, &identity) ) {
		S->error = PossumPipe_error_no_identity;
		netbufr->reset(netbufr);
		duct->send_Buffer(duct, netbufr);
		goto done;
	}
	S->client_identity = identity;


	/* Verify and decode packet. */
	INIT(HurdLib, Buffer, nonce, goto done);
	INIT(NAAAIM, PossumPacket, packet, goto done);
	if ( !packet->decode_packet2(packet, identity, S->software, \
				     netbufr) ) {
		S->error = PossumPipe_error_invalid_identity;
		goto done;
	}

	/* Create the perturbed software status. */
	if ( (b = packet->get_element(packet, PossumPacket_hardware)) == NULL )
		ERR(goto done);
	nonce->add_Buffer(nonce, b);
	if ( !identity->decrypt(identity, nonce) )
		ERR(goto done);

	if ( (hmac = NAAAIM_SHA256_hmac_Init(nonce)) == NULL )
		ERR(goto done);

	if ( !hmac->add_Buffer(hmac, S->software) )
		ERR(goto done);
	if ( !hmac->compute(hmac) )
		ERR(goto done);

	S->software->reset(S->software);
	if ( !S->software->add_Buffer(S->software, hmac->get_Buffer(hmac)) )
		ERR(goto done);

	if ( S->debug ) {
		fprintf(stdout, "\n%s: Incoming client packet 1:\n", __func__);
		packet->print(packet);
	}


	/* Extract the replay and quote nonces supplied by client. */
	nonce->reset(nonce);
	INIT(HurdLib, Buffer, quote_nonce, goto done);
	if ( (b = packet->get_element(packet, PossumPacket_replay_nonce)) \
	     == NULL )
		ERR(goto done);
	if ( !nonce->add_Buffer(nonce, b) )
		ERR(goto done);
	if ( (b = packet->get_element(packet, PossumPacket_quote_nonce)) \
	     == NULL )
		ERR(goto done);

	if ( !quote_nonce->add_Buffer(quote_nonce, b) )
		ERR(goto done);

	/* Verify hardware quote. */

	/* Verify protocol. */

	INIT(HurdLib, Buffer, public, goto done);
	if ( (b = packet->get_element(packet, PossumPacket_public)) == NULL )
		ERR(goto done);
	if ( !public->add_Buffer(public, b) )
		ERR(goto done);

	/* Generate DH public key for shared secret. */
	INIT(NAAAIM, Curve25519, dhkey, goto done);
	dhkey->generate(dhkey);

	packet->reset(packet);
	netbufr->reset(netbufr);

	packet->set_schedule2(packet, identity, time(NULL));
	packet->create_packet2(packet, identity, dhkey, NULL, spid);

	/* Reset the section back to the original. */
	if ( !packet->encode_packet1(packet, S->software, netbufr) )
		ERR(goto done);

	if ( S->debug ) {
		fprintf(stdout, "\n%s: Sending host packet 1:\n", __func__);
		packet->print(packet);
	}

	if ( !duct->send_Buffer(duct, netbufr) )
		ERR(goto done);

	if ( (our_nonce = packet->get_element(packet,			  \
					      PossumPacket_replay_nonce)) \
	     == NULL )
		ERR(goto done);
	if ( !generate_shared_keys(this, our_nonce, nonce, dhkey, public) ) {
		fputs("Failed key generation.\n", stderr);
		ERR(goto done);
	}

	/* Wait for platform verification reference quote. */
	netbufr->reset(netbufr);
	if ( (b = packet->get_element(packet, PossumPacket_quote_nonce)) == \
	     NULL )
		ERR(goto done);
	if ( !receive_platform_quote(this, netbufr, NULL, nonce) ) {
		if ( S->error == PossumPipe_error_internal )
			ERR(goto done);
		goto done;
	}

	/* Send platform verification quote. */
	netbufr->reset(netbufr);
	if ( !send_platform_report(this, netbufr, our_nonce, spid) )
		ERR(goto done);

	/* Get connection confirmation start. */
	netbufr->reset(netbufr);
	if ( !receive_connection_start(this, netbufr) ) {
		fputs("Failed connection start.\n", stderr);
		ERR(goto done);
	}

	if ( S->debug ) {
		fputs("\nShared 1:\n", stderr);
		S->shared1->print(S->shared1);
		fputs("Shared 2:\n", stderr);
		S->shared2->print(S->shared2);
	}

	S->sent->add(S->sent, S->shared1->get_Buffer(S->shared1));
	S->sent->add(S->sent, S->shared2->get_Buffer(S->shared2));
	if ( !S->sent->compute(S->sent) )
		ERR(goto done);
	if ( S->debug ) {
		fputs("\nTransmit root:\n", stderr);
		S->sent->print(S->sent);
	}

	S->received->add(S->received, S->shared2->get_Buffer(S->shared2));
	S->received->add(S->received, S->shared1->get_Buffer(S->shared1));
	if ( !S->received->compute(S->received) )
		ERR(goto done);
	if ( S->debug ) {
		fputs("Receive root:\n", stderr);
		S->received->print(S->received);
	}


	/* Setup host nonce generator. */
	if ( !_setup_nonce(S) )
		ERR(goto done);

	retn = true;


 done:
	WHACK(netbufr);
	WHACK(nonce);
	WHACK(quote_nonce);
	WHACK(public);
	WHACK(name);
	WHACK(remote_ip);
	WHACK(packet);
	WHACK(dhkey);
	WHACK(hmac);

	return retn;
}


/**
 * Private function.
 *
 * This function transmits confirmation for the host to move forward
 * with the secured connection.  The confirmation consists of
 * the SHA256 hash of the negotiated secret.
 *
 * \param duct		The network object which the reference quote is
 *			to be received on.
 *
 * \param bufr		The Buffer object which is to be used to send the
 *			confirmation message.
 *
 * \param key		The shared key used in the connextion.
 *
 *
 * \return		If an error is encountered in composing or
 *			sending the confirmation a false value is
 *			returned.  Success is indicated by returning
 *			true.
 */

static _Bool send_connection_start(CO(PossumPipe, this), CO(Buffer, bufr))

{
	STATE(S);

	_Bool retn = false;

	Buffer b,
	       key = S->shared2->get_Buffer(S->shared2);

	Duct duct = S->duct;

	AES256_cbc cipher = NULL;

	RandomBuffer iv = NULL;

	Sha256 sha256 = NULL;

	SHA256_hmac hmac = NULL;


	/* Generate the initialization vector. */
	INIT(NAAAIM, RandomBuffer, iv, goto done);
	if ( !iv->generate(iv, 16) )
		ERR(goto done);
	if ( !bufr->add_Buffer(bufr, iv->get_Buffer(iv)) )
		ERR(goto done);

	/* Generate the connection authenticator. */
	INIT(NAAAIM, Sha256, sha256, goto done);
	sha256->add(sha256, key);
	if ( !sha256->compute(sha256) )
		ERR(goto done);

	/* Encrypt the authenticator. */
	b = iv->get_Buffer(iv);
	if ( (cipher = NAAAIM_AES256_cbc_Init_encrypt(key, b)) == NULL )
		ERR(goto done);
	if ( !cipher->encrypt(cipher, sha256->get_Buffer(sha256)) )
		ERR(goto done);
	if ( !bufr->add_Buffer(bufr, cipher->get_Buffer(cipher)) )
		ERR(goto done);

	/* Add the authenticator checksum. */
	if ( (hmac = NAAAIM_SHA256_hmac_Init(key)) == NULL )
		ERR(goto done);
	hmac->add_Buffer(hmac, bufr);
	if ( !hmac->compute(hmac) )
		ERR(goto done);
	if ( !bufr->add_Buffer(bufr, hmac->get_Buffer(hmac)) )
		ERR(goto done);

	/* Send the authenticator. */
	if ( !duct->send_Buffer(duct, bufr) )
		ERR(goto done);

	retn = true;


 done:
	WHACK(iv);
	WHACK(cipher);
	WHACK(sha256);
	WHACK(hmac);

	return retn;
}


/**
 * External public method.
 *
 * This method implements handling the initiation and setup of a
 * connection to a remote server port.  Upon successful return the
 * object has a secured context established to the remote host.
 *
 * \param this		A pointer to the object which is to initiate
 *			a remote connection.
 *
 * \param spid		The object containing the service provider
 *			identity that is to be used to authenticate the
 *			host attestation quote.
 *
 * \return		A boolean value is returned to indicate the
 *			status of session initiation.  A false value
 *			indicates that connection setup failed while
 *			a true value indicates a session has been
 *			established and is valid.
 */

static _Bool start_client_mode(CO(PossumPipe, this), CO(Buffer, spid))

{
	STATE(S);

	_Bool retn = false;

	uint32_t spi = 0xfead;

	Duct duct = S->duct;

	SoftwareStatus software_status = NULL;

	Buffer b,
	       public,
	       their_nonce = NULL,
	       nonce	   = NULL,
	       quote_nonce = NULL,
	       bufr	   = NULL;

	String name = NULL;

	IDmgr idmgr = NULL;

	IDtoken token = NULL;

	Curve25519 dhkey = NULL;

	PossumPacket packet = NULL;

	Ivy ivy = NULL;


	/* Load identity token. */
	INIT(NAAAIM, IDtoken, token, goto done);
	INIT(NAAAIM, IDmgr, idmgr, goto done);
	if ( (name = HurdLib_String_Init_cstr("device")) == NULL )
		ERR(goto done);

	idmgr->attach(idmgr);
	if ( !idmgr->get_idtoken(idmgr, name, token) )
		ERR(goto done);

	/* Measure the current software state. */
	INIT(NAAAIM, SoftwareStatus, software_status, goto done);
	software_status->open(software_status);
	if ( !software_status->measure(software_status) )
		ERR(goto done);

	b = software_status->get_template_hash(software_status);
	if ( S->debug ) {
		fprintf(stdout, "\n%s: Local software status:\n", __func__, \
			stdout);
		b->print(b);
	}


	/* Send a session initiation packet. */
	INIT(HurdLib, Buffer, bufr, goto done);

	INIT(NAAAIM, Curve25519, dhkey, goto done);

	INIT(NAAAIM, PossumPacket, packet, goto done);
	packet->set_schedule(packet, token, time(NULL));
	dhkey->generate(dhkey);

#if 0
	tspi = propose_spi(CLIENT_SPI_ARENA, 0);
	spi = tspi << 16;
	tspi = propose_spi(HOST_SPI_ARENA, 0);
	spi = spi | tspi;
#endif
	packet->create_packet1(packet, token, dhkey, spi, spid);

	if ( !packet->encode_packet1(packet, b, bufr) )
		ERR(goto done);
	if ( !duct->send_Buffer(duct, bufr) )
		ERR(goto done);

	if ( S->debug ) {
		fprintf(stdout, "\n%s: Sent client packet 1:\n", __func__);
		packet->print(packet);
	}

	/* Save transmitted nonces for subsequent use. */
	INIT(HurdLib, Buffer, nonce, goto done);
	INIT(HurdLib, Buffer, quote_nonce, goto done);
	if ( (b = packet->get_element(packet, PossumPacket_quote_nonce)) == \
	     NULL )
		ERR(goto done);
	if ( !quote_nonce->add_Buffer(quote_nonce, b) )
		ERR(goto done);

	if ( (b = packet->get_element(packet, PossumPacket_replay_nonce)) \
	     == NULL )
		ERR(goto done);
	if ( !nonce->add_Buffer(nonce, b) )
		ERR(goto done);

	/* Wait for a packet to arrive. */
	packet->reset(packet);
	bufr->reset(bufr);
	if ( !duct->receive_Buffer(duct, bufr) ) {
		if ( duct->eof(duct) || (bufr->size(bufr) == 0) ) {
			S->error = PossumPipe_error_closed_pipe;
			goto done;
		}
		ERR(goto done);
	}

	if ( S->debug ) {
		fprintf(stdout, "\n%s: Received host initialization " \
			"packet:\n", __func__);
		bufr->hprint(bufr);
	}

	/* Find the host identity. */
	INIT(NAAAIM, Ivy, ivy, goto done);
	token->reset(token);
	if ( !find_client(bufr, token, ivy) ) {
		S->error = PossumPipe_error_no_identity;
		goto done;
	}

	/* Set the host configuration personality. */

#if 0
	/*
	 * See comments above in host_mode() as to disposition of this
	 * function
	 */
	if ( !set_counter_party_personality(cfg, token) )
	     ERR(goto done);
#endif

	if ( (b = ivy->get_element(ivy, Ivy_software)) == NULL )
		ERR(goto done);
	S->software->add_Buffer(S->software, b);

	if ( S->debug ) {
		fprintf(stdout, "\n%s: Using host software status:\n", \
			__func__, stdout);
		S->software->print(S->software);
	}

	if ( !packet->decode_packet1(packet, token, S->software, bufr) ) {
		S->error = PossumPipe_error_invalid_identity;
		goto done;
	}

	if ( S->debug ) {
		fprintf(stdout, "\n%s: Received host packet 1.\n", __func__);
		packet->print(packet);
	}

	if ( (b = packet->get_element(packet, PossumPacket_public)) == NULL )
		ERR(goto done);
	public = b;

	if ( (their_nonce = packet->get_element(packet,			    \
						PossumPacket_replay_nonce)) \
	     == NULL )
		ERR(goto done);
	bufr->reset(bufr);
	if ( !bufr->add_Buffer(bufr, their_nonce) )
		ERR(goto done);

	if ( !generate_shared_keys(this, bufr, nonce, dhkey, public) )
		ERR(goto done);

	/* Send platform reference. */
	if ( (b = packet->get_element(packet, PossumPacket_quote_nonce)) \
	     == NULL )
		ERR(goto done);
	bufr->reset(bufr);
	if ( !send_platform_quote(this, bufr, nonce, b) )
		ERR(goto done);

	/* Receive platform reference. */
	bufr->reset(bufr);
	if ( !receive_platform_quote(this, bufr, ivy, their_nonce) ) {
		if ( S->error == PossumPipe_error_internal )
			ERR(goto done);
		goto done;
	}

	if ( S->debug ) {
		fprintf(stdout, "\n%s: Verified server:\n", __func__);
		ivy->print(ivy);
	}

	/* Send initiation packet. */
	bufr->reset(bufr);
	if ( !send_connection_start(this, bufr) )
		ERR(goto done);

	if ( S->debug ) {
		fputs("\nShared 1:\n", stderr);
		S->shared1->print(S->shared1);
		fputs("Shared 2:\n", stderr);
		S->shared2->print(S->shared2);
	}

	S->sent->add(S->sent, S->shared2->get_Buffer(S->shared2));
	S->sent->add(S->sent, S->shared1->get_Buffer(S->shared1));
	if ( !S->sent->compute(S->sent) )
		ERR(goto done);
	if ( S->debug ) {
		fputs("\nTransmit root:\n", stderr);
		S->sent->print(S->sent);
	}

	S->received->add(S->received, S->shared1->get_Buffer(S->shared1));
	S->received->add(S->received, S->shared2->get_Buffer(S->shared2));
	if ( !S->received->compute(S->received) )
		ERR(goto done);
	if ( S->debug ) {
		fputs("Receive root:\n", stderr);
		S->received->print(S->received);
	}

	/* Setup client nonce generator. */
	if ( !_setup_nonce(S) )
		ERR(goto done);

	retn = true;

 done:
	WHACK(software_status);
	WHACK(nonce);
	WHACK(quote_nonce);
	WHACK(bufr);
	WHACK(idmgr);
	WHACK(token);
	WHACK(dhkey);
	WHACK(packet);
	WHACK(name);
	WHACK(ivy);

	return retn;
}


/**
 * External public method.
 *
 * This method implements handling the initiation and setup of a
 * connection to a remote server port using mode 2 authentication.
 * Mode 2 authention uses the modulus of a public key as the identity
 * of the entity making the host connection.  It is intended to be a
 * bootstrapping mechanism for establishing a host identity on the
 * platform.
 *
 * \param this		A pointer to the object which is to initiate
 *			a remote connection.
 *
 * \param id		The object containing the key that will be
 *			to authenticate the connection identity of the
 *			platform.
 *
 * \return		A boolean value is returned to indicate the
 *			status of session initiation.  A false value
 *			indicates that connection setup failed while
 *			a true value indicates a session has been
 *			established and is valid.
 */

static _Bool start_client_mode2(CO(PossumPipe, this), CO(RSAkey, id))

{
	STATE(S);

	_Bool retn = false;

	Duct duct = S->duct;

	Buffer b,
	       public,
	       their_nonce,
	       nonce	   = NULL,
	       quote_nonce = NULL,
	       bufr	   = NULL,
	       status	   = NULL;

	RandomBuffer rnd = NULL;

	Curve25519 dhkey = NULL;

	PossumPacket packet = NULL;

	SHA256_hmac hmac = NULL;


	/* Measure the current software state. */
	INIT(HurdLib, Buffer, status, ERR(goto done));
	if ( !status->add(status, mode2_status, sizeof(mode2_status)) )
		ERR(goto done);


	/* Send a session initiation packet. */
	INIT(NAAAIM, PossumPacket, packet, goto done);
	packet->set_schedule2(packet, id, time(NULL));

	INIT(NAAAIM, Curve25519, dhkey, goto done);
	dhkey->generate(dhkey);

	INIT(NAAAIM, RandomBuffer, rnd, ERR(goto done));
	rnd->generate(rnd, 256 / 8);

	INIT(HurdLib, Buffer, bufr, goto done);
	packet->create_packet2(packet, id, dhkey, rnd->get_Buffer(rnd), NULL);

	if ( !packet->encode_packet1(packet, status, bufr) )
		ERR(goto done);
	if ( !duct->send_Buffer(duct, bufr) )
		ERR(goto done);

	if ( S->debug ) {
		fprintf(stdout, "\n%s: Sent client packet 1:\n", __func__);
		packet->print(packet);
	}

	/* Save transmitted nonces for subsequent use. */
	INIT(HurdLib, Buffer, nonce, goto done);
	INIT(HurdLib, Buffer, quote_nonce, goto done);
	if ( (b = packet->get_element(packet, PossumPacket_quote_nonce)) == \
	     NULL )
		ERR(goto done);
	if ( !quote_nonce->add_Buffer(quote_nonce, b) )
		ERR(goto done);

	if ( (b = packet->get_element(packet, PossumPacket_replay_nonce)) \
	     == NULL )
		ERR(goto done);
	if ( !nonce->add_Buffer(nonce, b) )
		ERR(goto done);

	/* Wait for a packet to arrive. */
	packet->reset(packet);
	bufr->reset(bufr);
	if ( !duct->receive_Buffer(duct, bufr) ) {
		if ( duct->eof(duct) || (bufr->size(bufr) == 0) ) {
			S->error = PossumPipe_error_closed_pipe;
			goto done;
		}
		ERR(goto done);
	}

	if ( S->debug ) {
		fprintf(stdout, "\n%s: Received host initialization " \
			"packet:\n", __func__);
		bufr->hprint(bufr);
	}

	/* Set the host configuration personality. */
	if ( !S->software->add(S->software, mode2_status, \
			       sizeof(mode2_status)) )
		ERR(goto done);

	if ( (hmac = NAAAIM_SHA256_hmac_Init(rnd->get_Buffer(rnd))) == NULL )
		ERR(goto done);
	hmac->add_Buffer(hmac, S->software);
	if ( !hmac->compute(hmac) )
		ERR(goto done);

	S->software->reset(S->software);
	if ( !S->software->add_Buffer(S->software, hmac->get_Buffer(hmac)) )
		ERR(goto done);

	if ( S->debug ) {
		fprintf(stdout, "\n%s: Using host software status:\n", \
			__func__, stdout);
		S->software->print(S->software);
	}

	if ( !packet->decode_packet2(packet, id, S->software, bufr) ) {
		S->error = PossumPipe_error_invalid_identity;
		goto done;
	}

	if ( S->debug ) {
		fprintf(stdout, "\n%s: Received host packet 1.\n", __func__);
		packet->print(packet);
	}

	if ( (b = packet->get_element(packet, PossumPacket_public)) == NULL )
		ERR(goto done);
	public = b;

	if ( (their_nonce = packet->get_element(packet,			    \
						PossumPacket_replay_nonce)) \
	     == NULL )
		ERR(goto done);
	bufr->reset(bufr);
	if ( !bufr->add_Buffer(bufr, their_nonce) )
		ERR(goto done);

	if ( !generate_shared_keys(this, bufr, nonce, dhkey, public) )
		ERR(goto done);

	/* Send platform reference. */
	if ( (b = packet->get_element(packet, PossumPacket_quote_nonce)) \
	     == NULL )
		ERR(goto done);
	bufr->reset(bufr);
	if ( !send_platform_quote(this, bufr, nonce, b) )
		ERR(goto done);

	/* Receive platform reference. */
	bufr->reset(bufr);
	if ( !receive_platform_report(this, bufr, their_nonce) ) {
		if ( S->error == PossumPipe_error_internal )
			ERR(goto done);
		goto done;
	}

	if ( S->debug ) {
		fprintf(stdout, "\n%s: Verified server:\n", __func__);
	}

	/* Send initiation packet. */
	bufr->reset(bufr);
	if ( !send_connection_start(this, bufr) )
		ERR(goto done);

	if ( S->debug ) {
		fputs("\nShared 1:\n", stderr);
		S->shared1->print(S->shared1);
		fputs("Shared 2:\n", stderr);
		S->shared2->print(S->shared2);
	}

	S->sent->add(S->sent, S->shared2->get_Buffer(S->shared2));
	S->sent->add(S->sent, S->shared1->get_Buffer(S->shared1));
	if ( !S->sent->compute(S->sent) )
		ERR(goto done);
	if ( S->debug ) {
		fputs("\nTransmit root:\n", stderr);
		S->sent->print(S->sent);
	}

	S->received->add(S->received, S->shared1->get_Buffer(S->shared1));
	S->received->add(S->received, S->shared2->get_Buffer(S->shared2));
	if ( !S->received->compute(S->received) )
		ERR(goto done);
	if ( S->debug ) {
		fputs("Receive root:\n", stderr);
		S->received->print(S->received);
	}

	/* Setup client nonce generator. */
	if ( !_setup_nonce(S) )
		ERR(goto done);

	retn = true;


 done:
	WHACK(nonce);
	WHACK(quote_nonce);
	WHACK(bufr);
	WHACK(status);
	WHACK(rnd);
	WHACK(dhkey);
	WHACK(packet);
	WHACK(hmac);

	return retn;
}


/**
 * External public method.
 *
 * This method validates that the established connection is consistent
 * with the specified identity constraints.
 *
 * \param this		A pointer to the object which is to implement the
 *			connection.
 *
 * \param endpoints	The object containing the array of endpoint
 *			descriptors that the connection is to be validated
 *			against.
 *
 * \param status	A pointer to a boolean value that will be updated
 *			with whether or not a security context should
 *			be created for the endpoint.
 *
 * \return	A boolean value is returned to indicate
 *		indicate that this method should not be invoked from
 *		standard userspace.
 */

static _Bool verify(CO(PossumPipe, this), CO(Buffer, endpoints), _Bool *status)

{
	STATE(S);

	_Bool failed,
	      retn = false;

	unsigned int cnt;

	struct SGX_reportbody *bp;

	struct SRDEendpoint *ep;

	struct SRDE_quote *quote;


	/* Verify object and arguement status. */
	if ( S->poisoned )
		ERR(goto done);
	if ( endpoints == NULL )
		ERR(goto done);
	if ( endpoints->poisoned(endpoints) )
		ERR(goto done);
	if ( (endpoints->size(endpoints) % sizeof(struct SRDEendpoint)) != 0 )
		ERR(goto done);


	/* Generate pointer to report body. */
	quote = S->remote->get_quoteinfo(S->remote);
	bp = &quote->report_body;


	/* Loop over and verify the defined endpoints. */
	ep 	= (struct SRDEendpoint *) endpoints->get(endpoints);
	cnt	= endpoints->size(endpoints) / sizeof(struct SRDEendpoint);
	*status = false;

	while ( cnt-- ) {
		failed = false;

		if ( ep->mask & SRDEendpoint_attribute ) {
			if ( bp->attributes.flags != ep->attributes )
				failed = true;
		}
		if ( ep->mask & SRDEendpoint_vendor ) {
			if ( bp->isvprodid != ep->isv_id )
				failed = true;
		}
		if ( ep->mask & SRDEendpoint_svn ) {
			if ( bp->isvsvn < ep->isv_svn )
				failed = true;
		}

		if ( ep->mask & SRDEendpoint_mrsigner ) {
			if ( memcmp(bp->mrsigner, ep->mrsigner, \
				    sizeof(bp->mrsigner) != 0) )
				failed = true;
		}

		if ( ep->mask & SRDEendpoint_mrenclave ) {
			if ( memcmp(bp->mr_enclave.m, ep->mrsigner, \
				    sizeof(bp->mr_enclave.m) != 0) )
				failed = true;
		}

		if ( !failed ) {
			*status = true;
			retn	= true;
			goto done;
		}

		++ep;
	}

	retn = true;


 done:
	if ( !retn )
		S->poisoned = true;

	return retn;
}


/**
 * External public method.
 *
 * This method implements a method for obtaining the identity of the
 * client that initiated the security context.  It is connection mode
 * specific and at the current time only mode 2 connection identifiers
 * are supported.  In the case of a mode 2 connection the client
 * identity is a pointer to the RSAkey object that authenticates the
 * client.
 *
 * \param this		The communications object whose connection
 *			endpoint identity is to be queried.
 *
 * \return	A void pointer to the identity object that negotiated
 *		the security context.  The default initialize value
 *		is a NULL pointer.
 */

static void * get_client(CO(PossumPipe, this))

{
	STATE(S);

	return S->client_identity;
}


/**
 * External public method.
 *
 * This method implements a method for obtaining the status of the remote
 * enclave that the object is connected to.
 *
 * \param this		The communications object whose connection endpoint
 *			information is to be queried for.
 *
 * \param attributes	A pointer to a variable that will be loaded with
 *			the attributes of the remote enclave.
 *
 * \param signer	The object that will be loaded with the signature
 *			of the signer of the enclave.
 *
 * \param measurement	The object that will be loaded with the measurement
 *			of the remote enclave.
 *
 * \param vendor	A pointer to the variable that will be loaded
 *			with the vendor identity for the enclave.
 *
 * \param svn		A pointer to the variable that will be loaded
 *			with the security version of the enclave.
 *
 * \return	A boolean value is used to indicate whether or not the
 *		returned information is valid.  A false value indicates
 *		that the information is valid while a true value indicates
 *		the information is valid.
 */

static _Bool get_connection(CO(PossumPipe, this), uint64_t *attributes,	 \
			    CO(Buffer, signer), CO(Buffer, measurement), \
			    uint16_t *vendor, uint16_t * svn)

{
	STATE(S);

	_Bool retn = false;

	struct SRDE_quote *quote;

	struct SGX_reportbody *bp;


	/* Verify object status. */
	if ( S->poisoned )
		ERR(goto done);
	if ( (signer == NULL) || signer->poisoned(signer) )
		ERR(goto done);
	if ( (measurement == NULL) || measurement->poisoned(measurement) )
		ERR(goto done);


	/* Generate pointer to report body. */
	quote = S->remote->get_quoteinfo(S->remote);
	bp = &quote->report_body;

	/* Abstract attributes and version information. */
	*attributes = bp->attributes.flags;
	*vendor	    = bp->isvprodid;
	*svn	    = bp->isvsvn;

	/* Get signer and measurement values. */
	if ( !measurement->add(measurement, bp->mr_enclave.m, \
			       sizeof(bp->mr_enclave.m)) )
		ERR(goto done);
	if ( !signer->add(signer, bp->mrsigner, sizeof(bp->mrsigner)) )
		ERR(goto done);

	retn = true;


 done:
	return retn;
}


/**
 * External public method.
 *
 * This method implements a method for displaying the current
 * attestation status of the remote connection.
 *
 * \param this		The communications object whose status is to
 *			be dumped.
 * \return	No return value is defined.
 */

static void display_connection(CO(PossumPipe, this))

{
	STATE(S);


	S->remote->dump_report(S->remote);
	return;
}


/**
 * External public method.
 *
 * This method implements an accessor mode for returning the internal
 * error state of the communications object.   This allows an enclave
 * implementing a PossumPipe object to determine the failure mode of
 * the communications object.
 *
 * \param this		The communications object whose status is to
 *			be dumped.

 * \return		A PossumPipe_error_code value is returned from
 *			the current object state.
 */

static PossumPipe_error_code get_error(CO(PossumPipe, this))

{
	STATE(S);

	return S->error;
}


/**
 * External public method.
 *
 * This method implements a control method for setting the debug
 * status of a PossumPipe instance.
 *
 * \param this		the communications object whose debug status
 *			is to be set.
 *
 * \param debug		The value of the debug flag to be set.
 *
 * \return	No return value is defined.
 */

static void debug(CO(PossumPipe, this), const _Bool debug)

{
	STATE(S);


	S->debug = debug;
	return;
}


/**
 * External public method.
 *
 * This method implements resetting the secured communications object.
 * Conceptually this method is an extension method of the ->reset
 * function in the imbedded Duct object.
 *
 * In addition to resetting the Duct object this method clears all
 * the secure communications context in the state object.
 *
 * \param this	The communications object which is to be reset.
 *
 * \return	No return value is defined.
 */

static void reset(CO(PossumPipe, this))

{
	STATE(S);


	/* Reset the quoting object. */
	WHACK(S->remote);


	/* Clear the security state information. */
	if ( S->nonce != NULL )
		S->nonce->reset(S->nonce);

	S->shared1->reset(S->shared1);
	S->shared2->reset(S->shared2);

	if ( S->sent != NULL )
		S->sent->reset(S->sent);
	if ( S->received != NULL )
		S->received->reset(S->received);

	if ( S->software != NULL )
		S->software->reset(S->software);

	/* Reset the client identity. */
	S->client_identity = NULL;

	/* Close the underlying communications object. */
	S->duct->reset(S->duct);

	return;
}


/**
 * External public method.
 *
 * This method implements a destructor for a PossumPipe object.
 *
 * \param this	A pointer to the object which is to be destroyed.
 */

static void whack(CO(PossumPipe, this))

{
	STATE(S);


	S->duct->whack(S->duct);
	WHACK(S->remote);

	WHACK(S->nonce);

	S->shared1->whack(S->shared1);
	S->shared2->whack(S->shared2);

	WHACK(S->sent);
	WHACK(S->received);

	WHACK(S->software);

	S->root->whack(S->root, this, S);
	return;
}


/**
 * External constructor call.
 *
 * This function implements a constructor call for a PossumPipe object.
 *
 * \return	A pointer to the initialized PossumPipe.  A null value
 *		indicates an error was encountered in object generation.
 */

extern PossumPipe NAAAIM_PossumPipe_Init(void)

{
	auto Origin root;

	auto PossumPipe this = NULL;

	auto struct HurdLib_Origin_Retn retn;


	/* Get the root object. */
	root = HurdLib_Origin_Init();

	/* Allocate the object and internal state. */
	retn.object_size  = sizeof(struct NAAAIM_PossumPipe);
	retn.state_size   = sizeof(struct NAAAIM_PossumPipe_State);
	if ( !root->init(root, NAAAIM_LIBID, NAAAIM_PossumPipe_OBJID, &retn) )
		return NULL;
	this	    	  = retn.object;
	this->state 	  = retn.state;
	this->state->root = root;

	/* Initialize aggregate objects. */
	INIT(NAAAIM, Duct,   this->state->duct,	     goto fail);
	INIT(NAAAIM, Sha256, this->state->nonce,     goto fail);
	INIT(NAAAIM, Sha256, this->state->shared1,   goto fail);
	INIT(NAAAIM, Sha256, this->state->shared2,   goto fail);
	INIT(NAAAIM, Sha256, this->state->sent,	     goto fail);
	INIT(NAAAIM, Sha256, this->state->received,  goto fail);
	INIT(HurdLib, Buffer, this->state->software, goto fail);

	/* Initialize object state. */
	_init_state(this->state);

	/* Method initialization. */
	this->init_server = init_server;
	this->init_client = init_client;

	this->accept_connection = accept_connection;

	this->send_packet    = send_packet;
	this->receive_packet = receive_packet;

	this->start_host_mode	= start_host_mode;
	this->start_client_mode = start_client_mode;

	this->start_host_mode2	 = start_host_mode2;
	this->start_client_mode2 = start_client_mode2;

	this->verify		 = verify;
	this->get_client	 = get_client;
	this->get_connection	 = get_connection;
	this->display_connection = display_connection;

	this->get_error = get_error;

	this->debug = debug;
	this->reset = reset;
	this->whack = whack;

	return this;


 fail:
	WHACK(this->state->duct);

	root->whack(root, this, this->state);
	return NULL;

}

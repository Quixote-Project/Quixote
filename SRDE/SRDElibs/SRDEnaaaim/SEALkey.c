/** \file
 * This file contains the implementation of an object that is used to
 * generate enclave specific sealing keys.  The following two major
 * sealing keys are generated:
 *
 *	MRENCLAVE
 *
 *	MRSIGNER
 *
 * MRENCLAVE based seal keys are specific to the measurement of the
 * enclave and thus are extremely fungible and only have usefulness
 * within a specific execution envelope of an enclave.
 *
 * The MRSIGNER based seal keys are specific to the identity of the
 * enclave signer.  These keys are typically used to persist data in
 * between invocations of an enclave.
 *
 * The enclave sealing keys dependent on the CPU security version as
 * well as the security version of the enclave.  Newer versions of an
 * enclave, with respect to those identity elements, can re-generate
 * keys that were generated by previous versions of the enclave.  The
 * older enclaves cannot generate sealing keys that are relevant in
 * later versions of the enclave.
 *
 * This construct prevents sealed data elements from being accessed by
 * enclaves or a platform with known security vulnerabilities.
 */

/**************************************************************************
 * (C)Copyright IDfusion, LLC. All rights reserved.
 *
 * Please refer to the file named Documentation/COPYRIGHT in the top of
 * the source tree for copyright and licensing information.
 **************************************************************************/

/* Include files. */
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <Origin.h>
#include <HurdLib.h>
#include <Buffer.h>

#include <SRDE.h>
#include <SRDEfusion.h>

#include "NAAAIM.h"
#include "RandomBuffer.h"
#include "SHA256.h"
#include "SEALkey.h"


/* Object state extraction macro. */
#define STATE(var) CO(SEALkey_State, var) = this->state

/* Verify library/object header file inclusions. */
#if !defined(NAAAIM_LIBID)
#error Library identifier not defined.
#endif

#if !defined(NAAAIM_SEALkey_OBJID)
#error Object identifier not defined.
#endif


/** SEALkey private state information. */
struct NAAAIM_SEALkey_State
{
	/* The root object. */
	Origin root;

	/* Library identifier. */
	uint32_t libid;

	/* Object identifier. */
	uint32_t objid;

	/* Object status. */
	_Bool poisoned;

	/* The object containing the key identifier. */
	Buffer keyid;

	/* The object containing an initialization vector for the key. */
	Buffer keyiv;

	/* The object containing the generated key. */
	Buffer key;
};


/**
 * Internal private method.
 *
 * This method is responsible for initializing the NAAAIM_SEALkey_State
 * structure which holds state information for each instantiated object.
 *
 * \param S A pointer to the object containing the state information which
 *        is to be initialized.
 */

static void _init_state(CO(SEALkey_State, S))

{

	S->libid = NAAAIM_LIBID;
	S->objid = NAAAIM_SEALkey_OBJID;

	S->poisoned = false;

	S->keyid = NULL;
	S->keyiv = NULL;
	S->key	 = NULL;

	return;
}


/**
 * External public method.
 *
 * This method implements the generation of a sealing key based on
 * the MRSIGNER value.
 *
 * \param this		A pointer to the object generating the key.
 *
 * \return	A boolean value is used to indicate the status of
 *		the requested key generation.  A false value indicates
 *		the
 */

static _Bool generate_mrsigner(CO(SEALkey, this))

{
	STATE(S);

	_Bool retn = false;

	uint8_t keydata[16] __attribute__((aligned(128)));

	char report_data[64] __attribute__((aligned(128)));

	struct SGX_report __attribute__((aligned(512))) report;

	struct SGX_targetinfo target;

	struct SGX_keyrequest keyrequest;

	Buffer rbp,
	       bufr = NULL;

	RandomBuffer randbufr = NULL;

	Sha256 sha256 = NULL;


	/* Verify object and arguement status. */
	if ( S->poisoned )
		ERR(goto done);


	/* Request a self report to get the measurement. */
	memset(&target, '\0', sizeof(struct SGX_targetinfo));
	memset(&report, '\0', sizeof(struct SGX_report));
	memset(report_data, '\0', sizeof(report_data));
	enclu_ereport(&target, &report, report_data);

	if ( !(report.body.attributes.flags  & 0x0000000000000001ULL) || \
	     !(report.body.attributes.flags  & 0x0000000000000002ULL) )
		ERR(goto done);


	/* Use the specified keyid or generate one. */
	memset(&keyrequest, '\0', sizeof(struct SGX_keyrequest));

	if ( S->keyid == NULL ) {
		INIT(HurdLib, Buffer, S->keyid, ERR(goto done));
		INIT(NAAAIM, RandomBuffer, randbufr, ERR(goto done));

		if ( !randbufr->generate(randbufr, 32) )
			ERR(goto done);
		rbp = randbufr->get_Buffer(randbufr);
		memcpy(keyrequest.keyid, rbp->get(rbp), \
		       sizeof(keyrequest.keyid));

		if ( !S->keyid->add_Buffer(S->keyid, rbp) )
			ERR(goto done);
	} else {
		memcpy(keyrequest.keyid, S->keyid->get(S->keyid), \
		       sizeof(keyrequest.keyid));
	}

	/* Request a signer based key. */
	memset(keydata, '\0', sizeof(keydata));

	keyrequest.keyname   = SGX_KEYSELECT_SEAL;
	keyrequest.keypolicy = SGX_KEYPOLICY_SIGNER;

	keyrequest.isvsvn     = report.body.isvsvn;
	keyrequest.config_svn = report.body.config_svn;

	memcpy(keyrequest.cpusvn, report.body.cpusvn, \
	       sizeof(keyrequest.cpusvn));

	keyrequest.attributes.flags = 0xFF0000000000000BULL;
	keyrequest.attributes.xfrm  = 0x0ULL;

	keyrequest.miscselect = 0xF0000000;

	/* Generate the derived base key. */
	if ( enclu_egetkey(&keyrequest, keydata) != 0 )
		ERR(goto done);

	/* Hash the derived base key to obtain the encryption key. */
	INIT(HurdLib, Buffer, bufr, ERR(goto done));
	INIT(NAAAIM, Sha256, sha256, ERR(goto done));

	if ( !bufr->add(bufr, keydata, sizeof(keydata)) )
		ERR(goto done);
	sha256->add(sha256, bufr);
	if ( !sha256->compute(sha256) )
		ERR(goto done);

	INIT(HurdLib, Buffer, S->key, ERR(goto done));
	rbp = sha256->get_Buffer(sha256);
	if ( !S->key->add_Buffer(S->key, rbp) )
		ERR(goto done);

	/* Hash the symmetric key to obtain the initialization vector. */
	if ( !sha256->rehash(sha256, 512) )
		ERR(goto done);

	INIT(HurdLib, Buffer, S->keyiv, ERR(goto done));
	if ( !S->keyiv->add(S->keyiv, rbp->get(rbp), 16) )
		ERR(goto done);

	retn = true;


 done:
	WHACK(bufr);
	WHACK(randbufr);
	WHACK(sha256);

	if ( !retn )
		S->poisoned = true;

	return retn;
}


/**
 * External public method.
 *
 * This method implements the ability to set the keyid for an instance
 * of seal key.  This is a necessary requirement in order to create
 * reproducible keys.
 *
 * \param this		A pointer to the object that will have its
 *			keyid set.
 *
 * \param keyid		A pointer to the object containing the
 *			keyid value that is to be set.
 *
 * \return	A boolean value is used to indicate the status of
 *		setting the keyid.  A false value indicates the keyid
 *		was not properly set.  A true value indicates a new
 *		keyid value has been registerd.
 */

static _Bool set_keyid(CO(SEALkey, this), CO(Buffer, keyid))

{
	STATE(S);

	_Bool retn = false;


	/* Check object status. */
	if ( S->poisoned )
		ERR(goto done);
	if ( keyid == NULL )
		ERR(goto done);
	if ( keyid->poisoned(keyid) )
		ERR(goto done);
	if ( keyid->size(keyid) != SGX_HASH_SIZE )
		ERR(goto done);

	/* Allocate the keyid object if it has not been initialized. */
	if ( S->keyid == NULL )
		INIT(HurdLib, Buffer, S->keyid, ERR(goto done));

	/* Add the keyid. */
	if ( !S->keyid->add_Buffer(S->keyid, keyid) )
		ERR(goto done);

	retn = true;


 done:
	if ( !retn )
		S->poisoned = true;

	return retn;
}


/**
 * External public method.
 *
 * This method implements the ability to get the keyid for an instance
 * of a seal key.  This is a necessary requirement in order to create
 * reproducible keys.
 *
 * \param this		A pointer to the object that will have its
 *			keyid set.
 *
 * \param keyid		A pointer to the object that the keyid will
 *			be loaded into.
 *
 * \return	A boolean value is used to indicate the status of
 *		fetching the keyid.  A false value indicates the
 *		supplied keyid object does not have a valid keyid.  A
 *		true value indicates the object has a valid keyid.
 */

static _Bool get_keyid(CO(SEALkey, this), CO(Buffer, keyid))

{
	STATE(S);

	_Bool retn = false;


	/* Check object status. */
	if ( S->poisoned )
		ERR(goto done);
	if ( S->keyid == NULL )
		ERR(goto done);
	if ( keyid == NULL )
		ERR(goto done);
	if ( keyid->poisoned(keyid) )
		ERR(goto done);

	/* Allocate the keyid object if it has not been initialized. */
	if ( !keyid->add_Buffer(keyid, S->keyid) )
		ERR(goto done);

	retn = true;


 done:
	if ( !retn )
		S->poisoned = true;

	return retn;
}


/**
 * External public method.
 *
 * This method implements an accessor method for retrieving the
 * initialization vector and key that were generated.
 *
 * \param this		A pointer to the object that will have its
 *			its keying components retrieved.
 *
 * \param iv		A pointer to the object that the
 *			initialization vector will be loaded into.
 *
 * \param key		A pointer to the object that the encryption
 *			key will be loaded into.
 *
 * \return	A boolean value is used to indicate the status of
 *		fetching the keying elements.  A false value
 *		indicates an error was encountered and neither of
 *		the supplied objects can be considered to have
 *		valid data in them.  A true value idnicates that the
 *		keying elements were successfully returned.
 */

static _Bool get_iv_key(CO(SEALkey, this), CO(Buffer, iv), CO(Buffer, key))

{
	STATE(S);

	_Bool retn = false;


	/* Check object status. */
	if ( S->poisoned )
		ERR(goto done);
	if ( S->keyiv == NULL )
		ERR(goto done);
	if ( S->key == NULL )
		ERR(goto done);

	if ( iv == NULL )
		ERR(goto done);
	if ( iv->poisoned(iv) )
		ERR(goto done);

	if ( key == NULL )
		ERR(goto done);
	if ( key->poisoned(key) )
		ERR(goto done);


	/* Load the necessary elements. */
	if ( !iv->add_Buffer(iv, S->keyiv) )
		ERR(goto done);
	if ( !key->add_Buffer(key, S->key) )
		ERR(goto done);

	retn = true;


 done:
	if ( !retn )
		S->poisoned = true;

	return retn;
}


/**
 * External public method.
 *
 * This method implements the output of the various key elements.
 *
 * \param this	A pointer to the object whose key elements are
 *		to be output.
 */

static void print(CO(SEALkey, this))

{
	STATE(S);


	/* Verify object status. */
	if ( S->poisoned ) {
		fputs("*POISONED*\n", stderr);
		return;
	}


	/* Output elements. */
	fputs("Keyid:\n", stdout);
	S->keyid->hprint(S->keyid);

	fputs("IV:\n", stdout);
	S->keyiv->hprint(S->keyiv);

	fputs("Key:\n", stdout);
	S->key->hprint(S->key);

	return;
}


/**
 * External public method.
 *
 * This method implements resetting the object so that it can be
 * used for a new key generation cycle.
 *
 * \param this	A pointer to the object which is to be reset.
 */

static void reset(CO(SEALkey, this))

{
	STATE(S);


	if ( S->keyid != NULL )
		S->keyid->reset(S->keyid);
	if ( S->keyiv != NULL )
		S->keyiv->reset(S->keyiv);
	if ( S->key != NULL )
		S->key->reset(S->key);


	return;
}


/**
 * External public method.
 *
 * This method implements a destructor for a SEALkey object.
 *
 * \param this	A pointer to the object which is to be destroyed.
 */

static void whack(CO(SEALkey, this))

{
	STATE(S);

	WHACK(S->keyid);
	WHACK(S->keyiv);
	WHACK(S->key);

	S->root->whack(S->root, this, S);
	return;
}


/**
 * External constructor call.
 *
 * This function implements a constructor call for a SEALkey object.
 *
 * \return	A pointer to the initialized SEALkey.  A null value
 *		indicates an error was encountered in object generation.
 */

extern SEALkey NAAAIM_SEALkey_Init(void)

{
	Origin root;

	SEALkey this = NULL;

	struct HurdLib_Origin_Retn retn;


	/* Get the root object. */
	root = HurdLib_Origin_Init();

	/* Allocate the object and internal state. */
	retn.object_size  = sizeof(struct NAAAIM_SEALkey);
	retn.state_size   = sizeof(struct NAAAIM_SEALkey_State);
	if ( !root->init(root, NAAAIM_LIBID, NAAAIM_SEALkey_OBJID, &retn) )
		return NULL;
	this	    	  = retn.object;
	this->state 	  = retn.state;
	this->state->root = root;

	/* Initialize object state. */
	_init_state(this->state);

	/* Method initialization. */
	this->generate_mrsigner = generate_mrsigner;

	this->set_keyid = set_keyid;
	this->get_keyid = get_keyid;

	this->get_iv_key  = get_iv_key;

	this->print = print;
	this->reset = reset;
	this->whack = whack;

	return this;
}

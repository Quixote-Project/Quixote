/** \file
 * This file contains the implementation of an object which manages
 * the communications enclave with an ISOidentity enclave via a
 * POSSUM conduit.
 */

/**************************************************************************
 * (C)Copyright 2018, IDfusion LLC. All rights reserved.
 *
 * Please refer to the file named COPYING in the top of the source tree
 * for licensing information.
 **************************************************************************/

/* Local defines. */
#define SGX_DEVICE "/dev/isgx"


/* Include files. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>

#include <Origin.h>
#include <HurdLib.h>
#include <Buffer.h>
#include <String.h>
#include <File.h>

#include <NAAAIM.h>
#include <Duct.h>

#include "../SGX/SGX.h"
#include "../SGX/SGXenclave.h"
#include <SGXquote.h>

#include <ContourPoint.h>
#include <ExchangeEvent.h>

#include "ISOmanager.h"
#include "ISOmanager-interface.h"


/* Object state extraction macro. */
#define STATE(var) CO(ISOmanager_State, var) = this->state

/* Verify library/object header file inclusions. */
#if !defined(NAAAIM_LIBID)
#error Library identifier not defined.
#endif

#if !defined(NAAAIM_ISOmanager_OBJID)
#error Object identifier not defined.
#endif


/** OCALL interface definitions. */
struct ocall1_interface {
	char* str;
} ocall1_string;

int ocall1_handler(struct ocall1_interface *interface)

{
	fprintf(stdout, "%s", interface->str);
	return 0;
}

struct ocall2_interface {
	int* ms_cpuinfo;
	int ms_leaf;
	int ms_subleaf;
};

static void cpuid(int *eax, int *ebx, int *ecx, int *edx)\

{
	__asm("cpuid\n\t"
	      /* Output. */
	      : "=a" (*eax), "=b" (*ebx), "=c" (*ecx), "=d" (*edx)
	      /* Input. */
	      : "0" (*eax), "2" (*ecx));

	return;
}


int ocall2_handler(struct ocall2_interface *pms)

{
	struct ocall2_interface *ms = (struct ocall2_interface *) pms;


	ms->ms_cpuinfo[0] = ms->ms_leaf;
	ms->ms_cpuinfo[2] = ms->ms_subleaf;

	cpuid(&ms->ms_cpuinfo[0], &ms->ms_cpuinfo[1], &ms->ms_cpuinfo[2], \
	      &ms->ms_cpuinfo[3]);

	return 0;
}

static const struct OCALL_api ocall_table = {
	4,
	{
		ocall1_handler,
		ocall2_handler,
		Duct_sgxmgr,
		SGXquote_sgxmgr,
	}
};


/** ExchangeEvent private state information. */
struct NAAAIM_ISOmanager_State
{
	/* The root object. */
	Origin root;

	/* Library identifier. */
	uint32_t libid;
	/* Object identifier. */
	uint32_t objid;

	/* Object status. */
	_Bool poisoned;

	/* Enclave error code. */
	int enclave_error;

	/* SGX enclave object. */
	SGXenclave enclave;
};


/**
 * Internal private method.
 *
 * This method is responsible for initializing the
 * NAAAIM_ISOmanager_State structure which holds state information
 * for the object.
 *
 * \param S	A pointer to the object containing the state information
 *		which is to be initialized.
 */

static void _init_state(CO(ISOmanager_State, S))

{
	S->libid = NAAAIM_LIBID;
	S->objid = NAAAIM_ISOmanager_OBJID;

	S->poisoned	 = false;
	S->enclave_error = 0;

	S->enclave = NULL;

	return;
}


/**
 * External public method.
 *
 * This method implements initializing the SGX enclave which will
 * implement the ISOmanager enclave.
 *
 * \param this 		A pointer to the object which will manage
 *			communications with the enclave.
 *
 * \param enclave	A null terminated buffer containing the name
 *			of the enclave to load.
 *
 * \param token		A null terminated buffer containing the name
 *			of the file containing the initialization
 *			token.
 *
 * \param debug		A boolean value that specifies whether or not
 *			the enclave is to be run in production or
 *			debug mode.
 *			
 *
 * \return	A boolean value is used to indicate whether or not
 *		the enclave was successfully loaded..  A false value
 *		indicates a failure while a true value indicates
 *	        the enclave was loaded.
 */

static _Bool load_enclave(CO(ISOmanager, this), CO(char *, enclave), \
			  CO(char *, token), _Bool debug)

{
	STATE(S);

	_Bool retn = false;


	/* Load and initialize the enclave. */
	INIT(NAAAIM, SGXenclave, S->enclave, ERR(goto done));

	if ( !S->enclave->setup(S->enclave, enclave, token, true) )
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
 * This method implements connecting to an instance of an SGX based
 * ISOidentity modeling engine.
 *
 * \param this 		A pointer to the object which will be
 *		        implementing the connection.
 *
 * \param hostname	A null terminated buffer containing the name
 *			of the host to connect to.
 *
 * \param port		The number of the port to connect to.
 *
 * \param spid		A pointer to a null-terminated character buffer
 *			containing the value of the Service Provider
 *			Identity that is to be used to generate the
 *			attestation quote.
 *
 * \param identity	A pointer to the object containing the identity
 *			token to be used to authenticate the connection.
 *
 * \param verifier	A pointer to the object containing the identity
 *			verifier to be used to authenticate the remote
 *			host.
 *
 * \return	A boolean value is used to indicate whether or not
 *		the enclave was successfully loaded..  A false value
 *		indicates a failure while a true value indicates
 *	        the enclave was loaded.
 */

static _Bool connect(CO(ISOmanager, this), char *hostname, \
		     const unsigned int port, char * spid, \
		     CO(Buffer, id_bufr), CO(Buffer, ivy))

{
	STATE(S);

	_Bool retn = false;

	int rc;

	struct ISOmanager_ecall0 ecall0;


	memset(&ecall0, '\0', sizeof(struct ISOmanager_ecall0));

	ecall0.debug_mode    = true;
	ecall0.port	     = port;
	ecall0.current_time  = time(NULL);

	ecall0.hostname	     = hostname;
	ecall0.hostname_size = strlen(hostname) + 1;

	ecall0.spid	     = spid;
	ecall0.spid_size     = strlen(spid) + 1;

	ecall0.identity	     = id_bufr->get(id_bufr);
	ecall0.identity_size = id_bufr->size(id_bufr);

	ecall0.verifier	     = ivy->get(ivy);
	ecall0.verifier_size = ivy->size(ivy);

	if ( !S->enclave->boot_slot(S->enclave, 0, &ocall_table, \
				    &ecall0, &rc) ) {
		fprintf(stderr, "Enclave returned: %d\n", rc);
		goto done;
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
 * This method implements execution of the ECALL which returns the
 * host specific identity of the enclave.
 *
 * \param this	A pointer to the enclave which is to be measured.
 *
 * \param bufr	The object which the enclave measurement is to be
 *		loaded into.
 *
 * \return	A boolean value is returned to indicate if the identity
 *		was successfully generated.  A false value indicates
 *		the generation failed while a true value indicates the
 *		buffer contains a valid enclave identity.
 *
 */

static _Bool generate_identity(CO(ISOmanager, this), CO(Buffer, bufr))

{
	STATE(S);

	_Bool retn = false;

	int rc;

	struct ISOmanager_ecall1 ecall1;

	
	/* Verify object status. */
	if ( S->poisoned )
		ERR(goto done);


	/* Call ECALL slot 1 to get the enclave identity. */
	memset(&ecall1, '\0', sizeof(struct ISOmanager_ecall1));

	if ( !S->enclave->boot_slot(S->enclave, 1, &ocall_table, &ecall1, \
				    &rc) ) {
		fprintf(stderr, "Enclave returned: %d\n", rc);
		goto done;
	}
	if ( !ecall1.retn )
		ERR(goto done);

	if ( !bufr->add(bufr, ecall1.id, sizeof(ecall1.id)) )
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
 * This method implements a destructor for an ISOmanager object.
 *
 * \param this	A pointer to the object which is to be destroyed.
 */

static void whack(CO(ISOmanager, this))

{
	STATE(S);


	WHACK(S->enclave);

	S->root->whack(S->root, this, S);
	return;
}


/**
 * External constructor call.
 *
 * This function implements a constructor call for an ExchangeEvent object.
 *
 * \return	A pointer to the initialized exchange event.  A null value
 *		indicates an error was encountered in object generation.
 */

extern ISOmanager NAAAIM_ISOmanager_Init(void)

{
	Origin root;

	ISOmanager this = NULL;

	struct HurdLib_Origin_Retn retn;


	/* Get the root object. */
	root = HurdLib_Origin_Init();


	/* Allocate the object and internal state. */
	retn.object_size  = sizeof(struct NAAAIM_ISOmanager);
	retn.state_size   = sizeof(struct NAAAIM_ISOmanager_State);
	if ( !root->init(root, NAAAIM_LIBID, NAAAIM_ISOmanager_OBJID, &retn) )
		return NULL;
	this	    	  = retn.object;
	this->state 	  = retn.state;
	this->state->root = root;


	/* Initialize object state. */
	_init_state(this->state);


	/* Method initialization. */
	this->load_enclave	= load_enclave;
	this->connect		= connect;
	this->generate_identity = generate_identity;

	this->whack		= whack;

	return this;
}

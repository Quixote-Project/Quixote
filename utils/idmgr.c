/** \file
 * This file implements the identity manager daemon.  This daemon is
 * responsible for loading the device identity and extending the
 * measurement statement of the platform with that identity.
 *
 * The manager daemon provides OTI services to clients which request
 * the generation of a one time encryption key based on the device
 * identity.
 */

/**************************************************************************
 * (C)Copyright 2014, IDfusion, LLC. All rights reserved.
 *
 * Please refer to the file named COPYING in the top of the source tree
 * for licensing information.
 **************************************************************************/


/* Local defines. */
#define IDENTITY_NV_INDEX 0xbeaf
#define IDENTITY_PCR 11


/* Include files. */
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/reboot.h>
#include <sys/mount.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <Origin.h>
#include <HurdLib.h>
#include <Buffer.h>
#include <String.h>
#include <File.h>

#include <IDtoken.h>
#include <SHA256.h>

#include "TPMcmd.h"


/* Static variable definitions. */
static _Bool Have_SIGUSR1 = false;


/**
 * Private function.
 *
 * This function is responsible for initializating the identity of
 * the device.
 *
 * This is done by doing the following extension into PCR 19:
 *
 *	PCR10 || PCR17 || PCR18 || IdentityImplementation
 *
 * \param identity	The token which contains the implementation of
 *			the device's identity.
 *
 * \return		A false value indicates the identity was not
 *			properly initialized.  A true value indicates
 *			the machine identity state is properly initialized.
 */

static _Bool initialize_device_identity(CO(IDtoken, identity))

{
	char *err = NULL;

	Buffer bufr = NULL;

	TPMcmd tpm = NULL;


	INIT(NAAAIM, TPMcmd, tpm, goto done);
	INIT(HurdLib, Buffer, bufr, goto done);
	
	if ( !tpm->nv_read(tpm, IDENTITY_NV_INDEX, bufr) ) {
		err = "Unable to read NVram.";
		goto done;
	}
	if ( !identity->decode(identity, bufr) ) {
		err = "unable to decode identity.";
		goto done;
	}

	bufr->reset(bufr);
	tpm->pcr_read(tpm, 10, bufr);
	tpm->pcr_read(tpm, 17, bufr);
	tpm->pcr_read(tpm, 18, bufr);
	bufr->add_Buffer(bufr, identity->get_element(identity, IDtoken_id));

	if ( !tpm->pcr_extend(tpm, IDENTITY_PCR, bufr) ) {
		err = "Cannot extend PCR identity register.";
		goto done;
	}

 done:
	WHACK(tpm);
	WHACK(bufr);

	if ( err != NULL ) {
		fprintf(stderr, "%s\n", err);
		return false;
	}
	return true;
}


/**
 * Private function.
 *
 * This function is responsible for reducing the device identity into
 * a form which can be used for generating OTI keys
 *
 *
 * \param identity	The token which contains the implementation of
 *			the device's identity.
 *
 * \return		A false value indicates the identity was
 *			not properly reduced.  A true value indicates
 *			the identity is in reduced form.
 */

static _Bool reduce_identity(CO(IDtoken, identity))

{
	_Bool retn = false;

	Buffer b,
	       orgkey	= NULL,
	       orgid	= NULL,
	       idkey	= NULL;

	SHA256 sha256 = NULL;


	INIT(NAAAIM, SHA256, sha256, goto done);

	b = identity->get_element(identity, IDtoken_id);
	sha256->add(sha256, b);
	if ( !sha256->compute(sha256) )
		goto done;

	INIT(HurdLib, Buffer, orgkey, goto done);
	INIT(HurdLib, Buffer, orgid,  goto done);
	INIT(HurdLib, Buffer, idkey,  goto done);

	orgkey->add_Buffer(orgkey, identity->get_element(identity, \
							 IDtoken_orgkey));
	orgid->add_Buffer(orgid, identity->get_element(identity, \
						       IDtoken_orgid));
	idkey->add_Buffer(idkey, identity->get_element(identity, IDtoken_key));

	identity->reset(identity);
	identity->set_element(identity, IDtoken_orgkey, orgkey);
	identity->set_element(identity, IDtoken_orgid, orgid);
	identity->set_element(identity, IDtoken_id, \
			      sha256->get_Buffer(sha256));
	if ( identity->set_element(identity, IDtoken_key, idkey) )
		retn = true;


 done:
	WHACK(orgkey);
	WHACK(orgid);
	WHACK(idkey);

	return retn;
}


/**
 * Private function.
 *
 * This function implements the SIGUSR1 handler for the identity_manager
 * main process.  It simply sets the global Have_SIGUSR1 to true and
 * returns.
 */

void sigusr1_handler(int sig)

{
	if ( sig == SIGUSR1 )
		Have_SIGUSR1 = true;

	return;
}
		


/**
 * Private function.
 *
 * This function is responsible for implementing the identity manager
 * processing loop.  This function loops endlessly waiting for a
 * SIGUSR1 signal.  When it receives a signal it reads the request
 * for generation of an OTI key from the idmgr shared memory array
 * and writes the result back into the output area.
 *
 * \param identity	The token which the identity manager is
 *			implementing identity services for.
 *
 * \return		No return value is specified.
 */

static void identity_manager(CO(IDtoken, identity))

{
	struct sigaction signal_action;


	signal_action.sa_handler = sigusr1_handler;

	if ( sigaction(SIGUSR1, &signal_action, NULL) == -1 )
		goto done;

	while ( 1 ) {
		pause();
		if ( !Have_SIGUSR1 )
			continue;
		fputs("\n\nProcessing identity request.\n", stderr);
		identity->print(identity);
		Have_SIGUSR1 = false;
	}

 done:
	return;
}


/*
 * Program entry point begins here.
 */

extern int main(int argc, char *argv[])

{
	int retn = 1;

	IDtoken identity = NULL;


	INIT(NAAAIM, IDtoken, identity, goto done);

	if ( !initialize_device_identity(identity) ) {
		fputs("Failed to initialized identity.\n", stderr);
		goto done;
	}

	if ( !reduce_identity(identity) ) {
		fputs("Failed to store identity.\n", stderr);
		goto done;
	}

	identity_manager(identity);
	retn = 0;


 done:
	WHACK(identity);

	return retn;
}

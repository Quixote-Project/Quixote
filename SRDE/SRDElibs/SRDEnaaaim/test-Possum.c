/** \file
 * This file contains a test harness for exercising the functionality
 * of the enclave based PossumPipe object.
 */

/**************************************************************************
 * Copyright (c) Enjellic Systems Development, LLC. All rights reserved.
 *
 * Please refer to the file named Documentation/COPYRIGHT in the top of
 * the source tree for copyright and licensing information.
 **************************************************************************/


/* Program name and associated enclave. */
#define PGM		"test-Possum"
#define COPYRIGHT	"%s: Copyright (c) %s, %s. All rights reserved.\n"
#define DATE		"2020"
#define COMPANY		"Enjellic Systems Development, LLC"

#define ENCLAVE PGM".signed.so"


/* Include files. */
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <Origin.h>
#include <HurdLib.h>
#include <String.h>
#include <Buffer.h>
#include <File.h>

#include <NAAAIM.h>
#include <Duct.h>
#include <PossumPipe.h>
#include <IDtoken.h>

#include <SRDE.h>
#include <SRDEenclave.h>
#include <SRDEquote.h>
#include <SRDEocall.h>
#include <SRDEfusion-ocall.h>
#include <SRDEnaaaim-ocall.h>

#include "test-Possum-interface.h"


/**
 * Program entry point.
 *
 * The following arguements are processed:
 *
 *	-d:	By default the debug attribute is set for the enclave.
 *		This option toggles that option.
 *
 *	-e:	The enclave which is to be executed.
 *
 *	-n:	The SGX device node to be used.  By default /dev/isgx.
 *
 *	-t:	The file containing the EINITTOKEN for this processor.
 */

extern int main(int argc, char *argv[])

{
	_Bool debug_mode    = false,
	      debug_enclave = true;

	char *id_token	   = NULL,
	     *verifier	   = NULL,
	     *spid_fname   = SPID_FILENAME,
	     *token	   = SGX_TOKEN_DIRECTORY"/test-Possum.token",
	     *hostname	   = "localhost",
	     *enclave_name = ENCLAVE_NAME;

	int opt,
	    rc,
	    retn = 1;

	enum {none, client, server, measure} Mode = none;

	FILE *idfile = NULL;

	Buffer bufr	= NULL,
	       ivy	= NULL,
	       id_bufr	= NULL,
	       vfy_bufr = NULL;

	String spid = NULL;

	IDtoken idt = NULL;

	File infile    = NULL,
	     spid_file = NULL;

	SRDEenclave enclave = NULL;

	SRDEocall ocall = NULL;

	struct OCALL_api *ocall_table;

	struct Possum_ecall0 ecall0;

	struct Possum_ecall1 ecall1;

	struct Possum_ecall2 ecall2;

	struct Possum_ecall3 ecall3;


	/* Parse and verify arguements. */
	while ( (opt = getopt(argc, argv, "CMSdph:i:s:t:v:")) != EOF )
		switch ( opt ) {
			case 'C':
				Mode = client;
				break;
			case 'M':
				Mode = measure;
				break;
			case 'S':
				Mode = server;
				break;

			case 'd':
				debug_mode = true;
				break;
			case 'p':
				debug_enclave = false;
				break;

			case 'h':
				hostname = optarg;
				break;
			case 'i':
				id_token = optarg;
				break;
			case 's':
				spid_fname = optarg;
				break;
			case 't':
				token = optarg;
				break;
			case 'v':
				verifier = optarg;
				break;
		}


	/* Validate that required arguements are present. */
	if ( Mode == none ) {
		fputs("No mode specified.\n", stderr);
		goto done;
	}


	/* Build the OCALL dispatch table. */
	INIT(NAAAIM, SRDEocall, ocall, ERR(goto done));

	ocall->add_table(ocall, SRDEfusion_ocall_table);
	ocall->add_table(ocall, SRDEnaaaim_ocall_table);

	if ( !ocall->get_table(ocall, &ocall_table) )
		ERR(goto done);


	/* Handle request for measurement. */
	if ( Mode == measure ) {
		INIT(NAAAIM, SRDEenclave, enclave, ERR(goto done));
		if ( !enclave->setup(enclave, enclave_name, token, \
				     debug_enclave) )
			ERR(goto done);

		memset(&ecall2, '\0', sizeof(struct Possum_ecall2));

		if ( !enclave->boot_slot(enclave, 2, ocall_table, \
					 &ecall2, &rc) ) {
			fprintf(stderr, "Enclave returned: %d\n", rc);
			goto done;
		}
		if ( !ecall2.retn ) {
			fputs("Enclave measurement failed.\n", stderr);
			goto done;
		}

		INIT(HurdLib, Buffer, bufr, ERR(goto done));
		if ( !bufr->add(bufr, ecall2.id, sizeof(ecall2.id)) ) {
			fputs("Unable to generate enclave value.\n", stderr);
			goto done;
		}
		bufr->print(bufr);

		goto done;
	}


	/* Output header. */
	fprintf(stdout, "%s: SGX SecurePipe test utility.\n", PGM);
	fprintf(stdout, COPYRIGHT, PGM, DATE, COMPANY);

	if ( verifier == NULL ) {
		fputs("No identifier verified specifed.\n", stderr);
		goto done;
	}

	if ( id_token == NULL ) {
		fputs("No device identity specifed.\n", stderr);
		goto done;
	}


	/* Setup the SPID. */
	INIT(HurdLib, String, spid, ERR(goto done));

	INIT(HurdLib, File, spid_file, ERR(goto done));
	if ( !spid_file->open_ro(spid_file, spid_fname) )
		ERR(goto done);
	if ( !spid_file->read_String(spid_file, spid) )
		ERR(goto done);

	if ( spid->size(spid) != 32 ) {
		fputs("Invalid SPID size: ", stdout);
		spid->print(spid);
		goto done;
	}


	/* Load the identity token. */
	INIT(NAAAIM, IDtoken, idt, goto done);
	if ( (idfile = fopen(id_token, "r")) == NULL ) {
		fputs("Cannot open identity token file.\n", stderr);
		goto done;
	}
	if ( !idt->parse(idt, idfile) ) {
		fputs("Enable to parse identity token.\n", stderr);
		goto done;
	}

	INIT(HurdLib, Buffer, id_bufr, ERR(goto done));
	if ( !idt->encode(idt, id_bufr) ) {
		fputs("Error encoding identity token.\n", stderr);
		goto done;
	}


	/* Load the identifier verifier. */
	INIT(HurdLib, Buffer, ivy, ERR(goto done));
	INIT(HurdLib, File, infile, ERR(goto done));

	infile->open_ro(infile, verifier);
	if ( !infile->slurp(infile, ivy) ) {
		fputs("Cannot read identity verifier.\n", stderr);
		goto done;
	}


	/* Load the enclave. */
	INIT(NAAAIM, SRDEenclave, enclave, ERR(goto done));
	if ( !enclave->setup(enclave, enclave_name, token, debug_enclave) )
		ERR(goto done);


	/* Load the identity verifiers. */
	memset(&ecall3, '\0', sizeof(struct Possum_ecall3));
	ecall3.verifier	     = ivy->get(ivy);
	ecall3.verifier_size = ivy->size(ivy);

	if ( !enclave->boot_slot(enclave, 3, ocall_table, \
				 &ecall3, &rc) ) {
		fprintf(stderr, "Ecall 3 returned: %d\n", rc);
		goto done;
	}


	/* Test server mode. */
	if ( Mode == server ) {
		memset(&ecall0, '\0', sizeof(struct Possum_ecall0));

		ecall0.debug_mode   = debug_mode;
		ecall0.port	    = 11990;
		ecall0.current_time = time(NULL);

		ecall0.spid	 = spid->get(spid);
		ecall0.spid_size = spid->size(spid) + 1;

		ecall0.identity	     = id_bufr->get(id_bufr);
		ecall0.identity_size = id_bufr->size(id_bufr);

		ecall0.verifier	     = NULL;
		ecall0.verifier_size = 0;

		if ( !enclave->boot_slot(enclave, 0, ocall_table, \
					 &ecall0, &rc) ) {
			fprintf(stderr, "Ecall 0 returned: %d\n", rc);
			goto done;
		}
	}


	/* Test client mode. */
	if ( Mode == client ) {
		memset(&ecall1, '\0', sizeof(struct Possum_ecall0));

		ecall1.debug_mode    = debug_mode;
		ecall1.port	     = 11990;
		ecall1.current_time  = time(NULL);

		ecall1.hostname	     = hostname;
		ecall1.hostname_size = strlen(hostname) + 1;

		ecall1.spid	     = spid->get(spid);
		ecall1.spid_size     = spid->size(spid) + 1;

		ecall1.identity	     = id_bufr->get(id_bufr);
		ecall1.identity_size = id_bufr->size(id_bufr);

		ecall1.verifier	     = NULL;
		ecall1.verifier_size = 0;

		if ( !enclave->boot_slot(enclave, 1, ocall_table, \
					 &ecall1, &rc) ) {
			fprintf(stderr, "Ecall 1 returned: %d\n", rc);
			goto done;
		}
	}

	retn = 0;


 done:
	if ( idfile != NULL )
		fclose(idfile);

	WHACK(bufr);
	WHACK(ivy);
	WHACK(id_bufr);
	WHACK(vfy_bufr);
	WHACK(spid);
	WHACK(idt);
	WHACK(infile);
	WHACK(spid_file);
	WHACK(enclave);
	WHACK(ocall);

	return retn;

}

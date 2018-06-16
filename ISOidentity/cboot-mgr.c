/** \file
 *
 * This file implements a utility for managing a cboot instance.  This
 * utility connects to a cboot instance through a UNIX domain socket
 * created in the following location:
 *
 * /var/run/cboot.PIDNUM
 */

/**************************************************************************
 * (C)Copyright 2017, IDfusion, LLC. All rights reserved.
 *
 * Please refer to the file named COPYING in the top of the source tree
 * for licensing information.
 **************************************************************************/


/* Include files. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/un.h>

#include <HurdLib.h>
#include <Buffer.h>
#include <String.h>
#include <File.h>

#include <NAAAIM.h>
#include <Duct.h>
#include <Buffer.h>
#include <LocalDuct.h>
#include <IDtoken.h>
#include <ISOmanager.h>

#include <SGX.h>
#include <SGXquote.h>

#include "cboot.h"


/**
 * The following enumeration type specifies whether or not the
 * management engine should be contact through an SGX POSSUM connection
 * or via a local UNIX domain connection.
 */
 enum {
	 internal,
	 sgx,
	 measure
} Mode = internal;


/**
 * Private function.
 *
 * This function implements the receipt of a trajectory list from
 * the canister management daemon.  The protocol used is for the
 * management daemon to send the number of points in the trajectory
 * followed by each point in ASCII form.
 *
 * \param mgmt		The socket object used to communicate with
 *			the canister management instance.
 *
 * \param cmdbufr	The object used to process the remote command
 *			response.
 *
 * \return		A boolean value is returned to indicate the
 *			status of processing processing the trajectory
 *			list.  A false value indicates an error occurred
 *			while a true value indicates the response was
 *			properly processed.
 */

static _Bool receive_trajectory(CO(LocalDuct, mgmt), CO(Buffer, cmdbufr))

{
	_Bool retn = false;

	unsigned int cnt;


	/* Get the number of points. */
	cmdbufr->reset(cmdbufr);
	if ( !mgmt->receive_Buffer(mgmt, cmdbufr) )
		ERR(goto done);
	cnt = *(unsigned int *) cmdbufr->get(cmdbufr);
	fprintf(stderr, "Trajectory size: %u\n", cnt);


	/* Output each point. */
	while ( cnt ) {
		cmdbufr->reset(cmdbufr);
		if ( !mgmt->receive_Buffer(mgmt, cmdbufr) )
			ERR(goto done);
		fprintf(stdout, "%s\n", cmdbufr->get(cmdbufr));
		--cnt;
	}

	cmdbufr->reset(cmdbufr);
	retn = true;

 done:
	return retn;
}


/**
 * Private function.
 *
 * This function implements the receipt of a forenscis list from
 * the canister management daemon.  The protocol used is for the
 * management daemon to send the number of events in the forensics
 * patch followed by each event in ASCII form.
 *
 * \param mgmt		The socket object used to communicate with
 *			the canister management instance.
 *
 * \param cmdbufr	The object used to process the remote command
 *			response.
 *
 * \return		A boolean value is returned to indicate the
 *			status of processing processing the forensics
 *			list.  A false value indicates an error occurred
 *			while a true value indicates the response was
 *			properly processed.
 */

static _Bool receive_forensics(CO(LocalDuct, mgmt), CO(Buffer, cmdbufr))

{
	_Bool retn = false;

	unsigned int cnt;


	/* Get the number of points. */
	cmdbufr->reset(cmdbufr);
	if ( !mgmt->receive_Buffer(mgmt, cmdbufr) )
		ERR(goto done);
	cnt = *(unsigned int *) cmdbufr->get(cmdbufr);
	fprintf(stderr, "Forensics size: %u\n", cnt);


	/* Output each point. */
	while ( cnt ) {
		cmdbufr->reset(cmdbufr);
		if ( !mgmt->receive_Buffer(mgmt, cmdbufr) )
			ERR(goto done);
		fprintf(stdout, "%s\n", cmdbufr->get(cmdbufr));
		--cnt;
	}

	cmdbufr->reset(cmdbufr);
	retn = true;

 done:
	return retn;
}


/**
 * Private function.
 *
 * This function implements receipt and processing of the command
 * which was executed on the canister management daemon.
 *
 * \param mgmt		The socket object used to communicate with
 *			the canister management instance.
 *
 * \param cmdbufr	The object used to hold the remote command
 *			response.
 *
 * \return		A boolean value is returned to indicate an
 *			error was encountered while processing receipt
 *			of the command.  A false value indicates an
 *			error occurred while a true value indicates the
 *			response was properly processed.
 */

static _Bool receive_command(CO(LocalDuct, mgmt), CO(Buffer, cmdbufr), \
			     int cmdnum)

{
	_Bool retn = false;


	switch ( cmdnum ) {
		case show_measurement:
			if ( !mgmt->receive_Buffer(mgmt, cmdbufr) )
				ERR(goto done);
			cmdbufr->print(cmdbufr);
			cmdbufr->reset(cmdbufr);
			retn = true;
			break;

		case show_trajectory:
			retn = receive_trajectory(mgmt, cmdbufr);
			break;

		case show_forensics:
			retn = receive_forensics(mgmt, cmdbufr);
			break;
	}

 done:
	return retn;
}


/**
 * Private function.
 *
 * This function implements the parsing of the supplied command and
 * the translation of this command to a binary expression of the
 * command.  The binary command is sent over the command socket and
 * the socket is read for the command response.
 *
 * \param mgmt		The socket object used to communicate with
 *			the canister management instance.
 *
 * \param cmd		A character point to the null-terminated buffer
 *			containing the ASCII version of the command.
 *
 * \return		A boolean value is returned to indicate whether
 *			or not processing of commands should continue.  A
 *			false value indicates the processing of commands
 *			should be terminated while a true value indicates
 *			an additional command cycle should be processed.
 */

static _Bool process_command(CO(LocalDuct, mgmt), CO(char *, cmd))

{
	_Bool retn = false;

	int lp,
	    cmdnum = 0;

	struct cboot_cmd_definition *cp = cboot_cmd_list;

	Buffer cmdbufr = NULL;


	/* Locate the command. */
	for (lp= 0; cp[lp].syntax != NULL; ++lp) {
		if ( strcmp(cp[lp].syntax, cmd) == 0 )
			cmdnum = cp[lp].command;
	}
	if ( cmdnum == 0 ) {
		fprintf(stdout, "Unknown command: %s\n", cmd);
		fflush(stdout);
		retn = true;
		goto done;
	}

	/* Send the command over the management socket. */
	INIT(HurdLib, Buffer, cmdbufr, ERR(goto done));

	cmdbufr->add(cmdbufr, (unsigned char *) &cmdnum, sizeof(cmdnum));
	if ( !mgmt->send_Buffer(mgmt, cmdbufr) )
		ERR(goto done);

	cmdbufr->reset(cmdbufr);
	if ( !receive_command(mgmt, cmdbufr, cmdnum) )
		ERR(goto done);
	retn = true;


 done:
	WHACK(cmdbufr);
	return retn;
}


/*
 * Program entry point begins here.
 */

extern int main(int argc, char *argv[])

{
	_Bool debug = true;

	char *p,
	     *spid     = NULL,
	     *canister = NULL,
	     *id_token = NULL,
	     *verifier = NULL,
	     *token	   = "ISOmanager.token",
	     *hostname	   = "localhost",
	     *enclave_name = "ISOmanager.signed.so",
	     sockname[UNIX_PATH_MAX],
	     inbufr[1024];

	int opt,
	    retn = 1;

	FILE *idfile = NULL;

	Buffer ivy     = NULL,
	       id_bufr = NULL,
	       cmdbufr = NULL;

	LocalDuct mgmt = NULL;

	IDtoken idt = NULL;

	File infile = NULL;

	ISOmanager enclave = NULL;


	while ( (opt = getopt(argc, argv, "MSpe:h:i:n:s:t:v:")) != EOF )
		switch ( opt ) {
			case 'M':
				Mode = measure;
				break;
			case 'S':
				Mode = sgx;
				break;

			case 'e':
				enclave_name = optarg;
				break;
			case 'h':
				hostname = optarg;
				break;
			case 'i':
				id_token = optarg;
				break;
			case 'n':
				canister = optarg;
				break;
			case 'p':
				debug = false;
				break;
			case 's':
				spid = optarg;
				break;
			case 't':
				token = optarg;
				break;
			case 'v':
				verifier = optarg;
				break;
		}


	/* Run measurement mode. */
	if ( Mode == measure ) {
		INIT(NAAAIM, ISOmanager, enclave, ERR(goto done));
		if ( !enclave->load_enclave(enclave, enclave_name, token, \
					    debug) )
			ERR(goto done);

		INIT(HurdLib, Buffer, id_bufr, ERR(goto done));
		if ( !enclave->generate_identity(enclave, id_bufr) )
			ERR(goto done);
		id_bufr->print(id_bufr);

		goto done;
	}


	/* Setup for SGX based modeling. */
	if ( Mode == sgx ) {
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


		/* Initialize enclave. */
		INIT(NAAAIM, ISOmanager, enclave, ERR(goto done));
		if ( !enclave->load_enclave(enclave, enclave_name, token, \
					    debug) ) {
			fputs("Manager enclave initialization failure.\n", \
			      stderr);
			goto done;
		}


		/* Connect to the enclave. */
		if ( !enclave->connect(enclave, hostname, 11990, spid,
				       id_bufr, ivy) ) {
			fputs("Unable to connect to model manager.\n", \
			      stderr);
			goto done;
		}

		goto done;
	}

	if ( canister == NULL ) {
		fputs("No canister name specified.\n", stderr);
		goto done;
	}


	/* Setup the management socket. */
	if ( snprintf(sockname, sizeof(sockname), "%s.%s", SOCKNAME, canister)
	     >= sizeof(sockname) ) {
		fputs("Socket name overflow.\n", stderr);
		goto done;
	}

	if ( (mgmt = NAAAIM_LocalDuct_Init()) == NULL ) {
		fputs("Error creating management socket.\n", stderr);
		goto done;
	}

	if ( !mgmt->init_client(mgmt) ) {
		fputs("Cannot set socket client mode.\n", stderr);
		goto done;
	}

	if ( !mgmt->init_port(mgmt, sockname) ) {
		fputs("Cannot initialize management port.\n", stderr);
		goto done;
	}


	/* Command loop. */
	INIT(HurdLib, Buffer, cmdbufr, ERR(goto done));

	while ( 1 ) {
		memset(inbufr, '\0', sizeof(inbufr));

		fputs("Cboot cmd>", stderr);
		if ( fgets(inbufr, sizeof(inbufr), stdin) == NULL )
			goto done;
		if ( (p = strchr(inbufr, '\n')) != NULL )
			*p = '\0';
		if ( strcmp(inbufr, "quit") == 0 ) {
			goto done;
		}

		if ( !process_command(mgmt, inbufr) )
			goto done;

	}


 done:
	if ( idfile != NULL )
		fclose(idfile);

	WHACK(ivy);
	WHACK(cmdbufr);
	WHACK(id_bufr);
	WHACK(mgmt);
	WHACK(idt);
	WHACK(infile);
	WHACK(enclave);

	return retn;
}

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
#include <Buffer.h>
#include <LocalDuct.h>

#include "cboot.h"


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


	if ( !mgmt->receive_Buffer(mgmt, cmdbufr) )
		ERR(goto done);

	switch ( cmdnum ) {
		case show_measurement:
			cmdbufr->print(cmdbufr);
			retn = true;
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
	char *sockname,
	     *p,
	     inbufr[1024];

	int opt,
	    retn = 1;

	Buffer cmdbufr = NULL;

	LocalDuct mgmt = NULL;


	while ( (opt = getopt(argc, argv, "f:")) != EOF )
		switch ( opt ) {
			case 'f':
				sockname = optarg;
				break;
		}


	/* Setup the management socket. */
	if ( (mgmt = NAAAIM_LocalDuct_Init()) == NULL ) {
		fputs("Error creating management socket.\n", stderr);
		goto done;
	}

	if ( !mgmt->init_client(mgmt) ) {
		fputs("Cannot set server mode.\n", stderr);
		goto done;
	}

	if ( !mgmt->init_port(mgmt, sockname) ) {
		fputs("Cannot initialize port.\n", stderr);
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
	WHACK(cmdbufr);
	WHACK(mgmt);

	return retn;
}

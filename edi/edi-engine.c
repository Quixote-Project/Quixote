/** \file
 * This file implements a server daemon which receives ASN1 encoded
 * EDI transactions and processes those transactions by either
 * decrypting or encrypting them with the identity specified in the
 * encoded request.
 */

/**************************************************************************
 * (C)Copyright 2015, IDfusion, LLC. All rights reserved.
 *
 * Please refer to the file named COPYING in the top of the source tree
 * for licensing information.
 **************************************************************************/


/* Include files. */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <arpa/inet.h>

#include <Origin.h>
#include <HurdLib.h>
#include <Config.h>
#include <Buffer.h>
#include <String.h>

#include <Duct.h>

#include "edi.h"


/* Variables static to this module. */
static pid_t process_table[100];


/**
 * Private function.
 *
 * This function initializes the process table.
 */

static void init_process_table(void)

{
	auto unsigned int lp;


	for (lp= 0; lp < sizeof(process_table)/sizeof(pid_t); ++lp)
		process_table[lp] = 0;
	return;
}


/**
 * Private function.
 *
 * This function adds an entry to the process state table.  It will
 * locate an empy slot in the table and place the PID of the dispatched
 * process in that slot.
 *
 * \param pid	The process ID to be placed in the table.
 */

static void add_process(pid_t pid)

{
	auto unsigned int lp;


	for (lp= 0; lp < sizeof(process_table)/sizeof(pid_t); ++lp)
		if ( process_table[lp] == 0 ) {
			process_table[lp] = pid;
			return;
		}
	return;
}


/**
 * Private function.
 *
 * This function reaps any available processes and updates its slot in
 * the process table.
 */

static void update_process_table(void)

{
	auto unsigned int lp;

	auto int pid,
		 status;


	while ( (pid = waitpid(-1, &status, WNOHANG)) > 0 )
		for (lp= 0; lp < sizeof(process_table)/sizeof(pid_t); ++lp)
			if ( process_table[lp] == pid ) {
				process_table[lp] = 0;
				fprintf(stdout, "%d terminated", pid);
				if ( !WIFEXITED(status) ) {
					fputs(" abnormally.\n", stdout);
					continue;
				}
				fprintf(stdout, ", status=%d\n", \
					WEXITSTATUS(status));
			}
	return;
}


/**
 * Private function.
 *
 * This function is called to handle a connection for an identity
 * generation request.
 *
 * \param duct	The network connection object being used to handle
 *		the identity generation request.
 *
 * \return	No return value is defined.
 */

static void handle_connection(CO(Duct,duct))

{
	const char *OK = "OK";

	pid_t id;

	Buffer bufr = NULL;


	INIT(HurdLib, Buffer, bufr, goto done);

	id = getpid();
	fprintf(stdout, "\n.%d: EDI engine connection, client=%s.\n", id, \
		duct->get_client(duct));

	while ( 1 ) {
		if ( !duct->receive_Buffer(duct, bufr) )
			goto done;
		fprintf(stderr, "%d: Processing EDI request:\n", id);

		bufr->reset(bufr);
		if ( !bufr->add(bufr, (unsigned char *) OK, strlen(OK)) )
			goto done;
		if ( !duct->send_Buffer(duct, bufr) )
			goto done;
	}


 done:
	WHACK(bufr);

	return;
}


/*
 * Program entry point begins here.
 */

extern int main(int argc, char *argv[])

{
	char *host = NULL,
	     *err  = NULL;

	int opt,
	    retn = 1;

	pid_t pid = 0;

	Duct duct = NULL;


	fputs("EDI engine started.\n", stdout);
	fflush(stdout);

	while ( (opt = getopt(argc, argv, "h:")) != EOF )
		switch ( opt ) {
			case 'h':
				host = optarg;
				break;
		}

	/* Arguement verification. */
	if ( host == NULL ) {
		fputs("No hostname specified.\n", stderr);
		goto done;
	}

	/* Initialize process table. */
	init_process_table();

	/* Initialize the network port and wait for connections. */
	INIT(NAAAIM, Duct, duct, goto done);

	if ( !duct->init_server(duct) ) {
		fputs("Cannot set server mode.\n", stderr);
		goto done;
	}

	if ( !duct->set_server(duct, host) ) {
		err = "Cannot set server name.";
		goto done;
	}

	if ( !duct->init_port(duct, NULL, ENGINE_PORT) ) {
		fputs("Cannot initialize port.\n", stderr);
		goto done;
	}

	while ( 1 ) {
		if ( !duct->accept_connection(duct) ) {
			err = "Error on connection accept.";
			goto done;
		}

		pid = fork();
		if ( pid == -1 ) {
			err = "Connection fork failure.";
			goto done;
		}
		if ( pid == 0 ) {
			handle_connection(duct);
			_exit(0);
		}

		add_process(pid);
		update_process_table();
		duct->reset(duct);
	}


 done:
	if ( err != NULL )
		fprintf(stderr, "!%s\n", err);

	if ( duct != NULL ) {
	     if ( !duct->whack_connection(duct) )
		     fputs("Error closing duct connection.\n", stderr);
	     duct->whack(duct);
	}

	if ( pid == 0 )
		fputs(".Client terminated.\n", stdout);

	return retn;
}

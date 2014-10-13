/** \file
 *
 * This file implements a command-line client for the identity generation
 * engine.  Based on command-line arguements which are specified it
 * issues a request to the management daemon for the generation of
 * an identity.
 */

/**************************************************************************
 * (C)Copyright 2014, IDfusion, LLC. All rights reserved.
 *
 * Please refer to the file named COPYING in the top of the source tree
 * for licensing information.
 **************************************************************************/

/* Include files. */
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/reboot.h>
#include <sys/mount.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <Origin.h>
#include <HurdLib.h>
#include <Buffer.h>
#include <String.h>

#include "IDengine.h"


/*
 * Program entry point begins here.
 */

extern int main(int argc, char *argv[])

{
	int retn = 1;

	Buffer id = NULL;

	IDengine idengine = NULL;


	INIT(HurdLib, Buffer, id, goto done);

	INIT(NAAAIM, IDengine, idengine, goto done);
	if ( !idengine->attach(idengine) ) {
		fputs("Failed attach\n", stderr);
		retn = 0;
		goto done;
	}

	if ( !idengine->get_identity(idengine, id) )
		goto done;
	fputs("identity:\n", stdout);
	id->print(id);
		

 done:
	WHACK(idengine);
	WHACK(id);

	return retn;
}

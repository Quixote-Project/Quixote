/** \file
 * This file implements a utility to execute TPM commands which are
 * specified as command-line arguements to the application.
 */

/**************************************************************************
 * (C)Copyright 2014, IDfusion, LLC. All rights reserved.
 *
 * Please refer to the file named COPYING in the top of the source tree
 * for licensing information.
 **************************************************************************/


#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <Origin.h>
#include <HurdLib.h>
#include <Buffer.h>
#include <String.h>
#include <File.h>

#include <NAAAIM.h>
#include <RandomBuffer.h>
#include "TPMcmd.h"


extern int main(int argc, char *argv[])

{
	int retn = 1,
	    index;

	Buffer bufr,
	       key;

	TPMcmd tpmcmd = NULL;

	File quote = NULL;

	RandomBuffer rbufr = NULL;


	if ( argv[1] == NULL ) {
		fputs("No TPM command specified.\n", stdout);
		goto done;
	}

	INIT(HurdLib, Buffer, bufr, goto done);
	INIT(HurdLib, Buffer, key, goto done);
	INIT(NAAAIM, TPMcmd, tpmcmd, goto done);

	if ( strcmp(argv[1], "pcrread") == 0 ) {
		if ( argv[2] == NULL ) {
			fputs("No PCR register specified.\n", stderr);
			goto done;
		}
		index = strtol(argv[2], NULL, 10);
                if ( errno == ERANGE )
                        goto done;
                if ( index < 0 )
                        goto done;

		if ( !tpmcmd->pcr_read(tpmcmd, index, bufr) ) {
			fputs("Failed PCR read.\n", stderr);
			goto done;
		}

		fprintf(stdout, "PCR-%02d: ", index);
		bufr->print(bufr);
		goto done;
	}

	if ( strcmp(argv[1], "pcrextend") == 0 ) {
		if ( argv[2] == NULL ) {
			fputs("No PCR register specified.\n", stderr);
			goto done;
		}
		index = strtol(argv[2], NULL, 10);
                if ( errno == ERANGE )
                        goto done;
                if ( index < 0 )
                        goto done;

		if ( argv[3] == NULL ) {
			fputs("No extension string specified.\n", stderr);
			goto done;
		}
		if ( !bufr->add(bufr, (unsigned char *) argv[3], \
				strlen(argv[3])) )
			goto done;

		if ( !tpmcmd->pcr_extend(tpmcmd, index, bufr) ) {
			fputs("Failed extend.\n", stderr);
			goto done;
		}
		fprintf(stdout, "Extended PCR-%02d: ", index);
		bufr->print(bufr);
	}

	if ( strcmp(argv[1], "nvread") == 0 ) {
		if ( argv[2] == NULL ) {
			fputs("No NVram index specified.\n", stderr);
			goto done;
		}
		index = strtol(argv[2], NULL, 0);
                if ( errno == ERANGE )
                        goto done;
                if ( index < 0 )
                        goto done;

		if ( !tpmcmd->nv_read(tpmcmd, index, bufr) )
			goto done;
		fprintf(stdout, "Contents of NVram index: 0x%x/%d\n", index, \
			index);
		bufr->hprint(bufr);
	}

	if ( strcmp(argv[1], "nvwrite") == 0 ) {
		index = strtol(argv[2], NULL, 0);
                if ( errno == ERANGE )
                        goto done;
                if ( index < 0 )
                        goto done;

		if ( argv[3] == NULL ) {
			fputs("No NVram password specified.\n", stderr);
			goto done;
		}
		if ( !key->add(key, (unsigned char *) argv[3], \
			       strlen(argv[3])) )
			goto done;

		if ( argv[4] == NULL ) {
			fputs("No NVram write string specified.\n", stderr);
			goto done;
		}
		if ( !bufr->add(bufr, (unsigned char *) argv[4], \
				strlen(argv[4])) )
			goto done;

		if ( !tpmcmd->nv_write(tpmcmd, index, bufr, false, key) )
			goto done;
		fprintf(stdout, "Wrote NVram index: 0x%x/%d\n", index, \
			index);
	}

	if ( strcmp(argv[1], "nvremove") == 0 ) {
		index = strtol(argv[2], NULL, 0);
                if ( errno == ERANGE )
                        goto done;
                if ( index < 0 )
                        goto done;

		if ( argv[3] == NULL ) {
			fputs("No NVram password specified.\n", stderr);
			goto done;
		}
		if ( !key->add(key, (unsigned char *) argv[3], \
			       strlen(argv[3])) )
			goto done;

		if ( !tpmcmd->nv_remove(tpmcmd, index, false, key) ) {
			fputs("Failed to remove NVram area.\n", stderr);
			goto done;
		}
		fprintf(stdout, "Removed NVram area %0x\n", index);
	}

	if ( strcmp(argv[1], "lspcr") == 0 ) {
		for(index=0; index < 24; ++index) {
			if ( !tpmcmd->pcr_read(tpmcmd, index, bufr) ) {
				fputs("Failed PCR read.\n", stderr);
				goto done;
			}
			fprintf(stdout, "PCR-%02d: ", index);
			bufr->print(bufr);
			bufr->reset(bufr);
		}
	}

	if ( strcmp(argv[1], "quote") == 0 ) {
		INIT(NAAAIM, RandomBuffer, rbufr, goto done);

		INIT(HurdLib, File, quote, goto done);
		if ( argv[2] == NULL ) {
			fputs("No aik file specified.\n", stderr);
			goto done;
		}
		quote->open_ro(quote, argv[2]);
		if ( !quote->slurp(quote, key) ) {
			fputs("Error reading aid uuid.\n", stderr);
			goto done;
		}

		rbufr->generate(rbufr, 20);
		if ( !bufr->add_Buffer(bufr, rbufr->get_Buffer(rbufr)) ) {
			fputs("Unable to generate nonce.\n", stderr);
			goto done;
		}
			
		if ( !tpmcmd->quote(tpmcmd, key, bufr) ) {
			fputs("Quote failed.\n", stderr);
			goto done;
		}
		fputs("Quote:\n", stderr);
		bufr->hprint(bufr);
		retn = 0;
	}


 done:
	WHACK(key);
	WHACK(bufr);
	WHACK(tpmcmd);
	WHACK(quote);
	WHACK(rbufr);

	return retn;
}

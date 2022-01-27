/** \file
 * This file implements a test driver for the SecurityEvent object
 * which models an information exchange event in a Turing Security
 * Event Model.
 */

/**************************************************************************
 * Copyright (c) Enjellic Systems Development, LLC. All rights reserved.
 *
 * Please refer to the file named Documentation/COPYRIGHT in the top of
 * the source tree for copyright and licensing information.
 **************************************************************************/


/* Include files. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ioctl.h>

#include <HurdLib.h>
#include <Buffer.h>
#include <String.h>
#include <File.h>

#include <NAAAIM.h>

#include "SecurityEvent.h"
#include "EventModel.h"


static uint8_t pseudonym[32] = {
	0x90, 0x95, 0x68, 0x80, 0x09, 0xb4, 0x15, 0xfa, \
	0xa8, 0x97, 0x63, 0x23, 0x1b, 0x82, 0xf2, 0x59, \
	0x56, 0xe7, 0x55, 0x28, 0x78, 0x94, 0xba, 0xb2, \
	0x32, 0x87, 0x64, 0xe1, 0xa8, 0x6d, 0x64, 0xf8 };


/**
 * Private function.
 *
 * This function is responsible for returning the current walltime
 * in milliseconds.
 *
 * \return		The epoch time in milli-seconds.
 */

static double wall_time(void)

{
	struct timeval walltime;


	if ( gettimeofday(&walltime, NULL) )
		return 0;

	return (double) (1000.0 * (double) walltime.tv_sec + \
			 (double) walltime.tv_usec / 1000.0);
}


/**
 * Private function.
 *
 * This function reads security interaction events from standard input
 * and prints the time required to parse the event.
 *
 * \return	No return value is defined.
 */

void test_file(void)

{
	char *p,
	     inbufr[1024];

	double start,
	       end;

	String evstr = NULL;

	SecurityEvent event = NULL;


	INIT(HurdLib, String, evstr, ERR(goto done));
	INIT(NAAAIM, SecurityEvent, event, ERR(goto done));

	while ( true ) {
		memset(inbufr, '\0', sizeof(inbufr));

		if ( fgets(inbufr, sizeof(inbufr), stdin) == NULL )
			goto done;
		if ( (p = strchr(inbufr, '\n')) != NULL )
			*p = '\0';
		if ( !evstr->add(evstr, inbufr) )
			ERR(goto done);

		start = wall_time();
		if ( !event->parse(event, evstr) )
			ERR(goto done);

		end = wall_time();
		fprintf(stdout, "start=%.1f, end=%1.f, time=%.1f\n", start, \
			end, end - start);

		evstr->reset(evstr);
		event->reset(event);
	}


 done:
	WHACK(evstr);
	WHACK(event);

	return;
}


/*
 * Program entry point begins here.
 */

extern int main(int argc, char *argv[])

{
	_Bool file_mode = false;

	int opt,
	    retn = 1;

	String entry = NULL;

	Buffer bufr = NULL;

	SecurityEvent event = NULL;

	EventModel event_model = NULL;


	while ( (opt = getopt(argc, argv, "F")) != EOF )
		switch ( opt ) {
			case 'F':
				file_mode = true;
				break;
		}


	/* Run utility in file mode. */
	if ( file_mode ) {
		test_file();
		return 0;
	}


	/* Test an individual event. */
	INIT(HurdLib, Buffer, bufr, ERR(goto done));

	INIT(HurdLib, String, entry, ERR(goto done));
	if ( !entry->add(entry, "event{swapper/0:/bin/bash-3.2.48} COE{uid=0, euid=0, suid=0, gid=0, egid=0, sgid=0, fsuid=0, fsgid=0, cap=3fffffffff} cell{uid=0, gid=0, mode=o100755, name_length=16, name=e1cb9766d47adb4d514d5590dd247504a3aab7e67839d65a6c6f4c32fc120e5d, s_id=xvda, s_uuid=feadbeaffeadbeaffeadbeaffeadbeaf, digest=d2a6bfe0d8a2346d45518dcaaf47642808d6c605506bd0b8e42a65a76735b98e}") )
		ERR(goto done);

	entry->print(entry);
	fputc('\n', stdout);


	INIT(NAAAIM, SecurityEvent, event, ERR(goto done));
	if ( !event->parse(event, entry) )
		ERR(goto done);
	if ( !event->measure(event) )
		ERR(goto done);
	if ( !event->get_identity(event, bufr) )
		ERR(goto done);
	event->dump(event);

	fputs("\nMeasurement:\n", stdout);
	bufr->print(bufr);

	fputs("\nEvent elements:\n", stdout);
	entry->reset(entry);
	if ( !event->format(event, entry) )
		ERR(goto done);
	entry->print(entry);

	/* Re-parse based on formatted event. */
	event->reset(event);
	if ( !event->parse(event, entry) )
		ERR(goto done);
	if ( !event->measure(event) )
		ERR(goto done);

	bufr->reset(bufr);
	if ( !event->get_identity(event, bufr) )
		ERR(goto done);
	fputs("\nMeasurement after re-parse:\n", stdout);
	bufr->print(bufr);


	/* Pseudonym processing. */
	INIT(NAAAIM, EventModel, event_model, ERR(goto done));

	bufr->reset(bufr);
	if ( !bufr->add(bufr, pseudonym, sizeof(pseudonym)) )
		ERR(goto done);

	fputs("\nTesting pseudonym:\n", stdout);
	bufr->print(bufr);

	if ( !event_model->add_pseudonym(event_model, bufr) )
		ERR(goto done);
	if ( !event_model->evaluate(event_model, event) )
		ERR(goto done);

	fputs("\nEvent elements after pseudonym processing:\n", stdout);
	entry->reset(entry);
	if ( !event->format(event, entry) )
		ERR(goto done);
	entry->print(entry);

	event->reset(event);
	if ( !event->parse(event, entry) )
		ERR(goto done);
	if ( !event->measure(event) )
		ERR(goto done);

	bufr->reset(bufr);
	if ( !event->get_identity(event, bufr) )
		ERR(goto done);
	fputs("\nMeasurement after digest processing:\n", stdout);
	bufr->print(bufr);

	retn = 0;


 done:
	WHACK(bufr);
	WHACK(entry);
	WHACK(event);
	WHACK(event_model);

	return retn;
}

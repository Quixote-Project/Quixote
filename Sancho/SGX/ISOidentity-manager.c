/** \file
 * This file contains the implementation of an ECALL which provides a
 * remote management interface for an ISOidentity modeling enclave.  The
 * management ECALL initiates a PossumPipe connection listening on
 * a specified port.  Commands received on this port are executed against
 * the model running in the enclave with results returned to the caller.
 */

/**************************************************************************
 * Copyright (c) Enjellic Systems Development, LLC. All rights reserved.
 *
 * Please refer to the file named Documentation/COPYRIGHT in the top of
 * the source tree for copyright and licensing information.
 **************************************************************************/


#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>

#include <HurdLib.h>
#include <Buffer.h>
#include <String.h>
#include <SHA256.h>

#include <NAAAIM.h>
#include <IDtoken.h>
#include <Ivy.h>
#include <RSAkey.h>
#include <PossumPipe.h>

#include <SRDE.h>
#include <SRDEfusion.h>
#include <SRDEfusion-ocall.h>
#include <SRDEnaaaim-ocall.h>

#include <cboot.h>

#include "SanchoSGX-interface.h"
#include "SecurityPoint.h"
#include "SecurityEvent.h"
#include "TSEM.h"


/**
 * A reference to the model object being maintained.
 */
extern TSEM Model;


/**
 * The device identity to be used.
 */
size_t Identity_size	= 0;
unsigned char *Identity = NULL;


/**
 * The list of verified management counter-parties.
 */
Buffer Verifiers = NULL;


/**
 * The seed time for the time() function.
 */
static time_t Current_Time;


/**
 * The debug status of the manager.
 */
static _Bool Debug;


/**
 * Private function.
 *
 * This function is responsible for returning the current trajectory
 * list to the management endpoint.  The protocol used is to send the
 * number of elements in the list followed by each point as an ASCII
 * string.
 *
 * \param mgmt		The object used to communicate with the canister
 *			management instance.
 *
 * \param cmdbufr	The object which will be used to hold the
 *			information which will be transmitted.
 *
 * \return		A boolean value is returned to indicate whether
 *			or not the command was processed.  A false value
 *			indicates the processing of commands should be
 *			terminated while a true value indicates an
 *			additional command cycle should be processed.
 */

static _Bool send_trajectory(CO(PossumPipe, mgmt), CO(Buffer, cmdbufr))

{
	_Bool retn = false;

	size_t lp,
	       cnt = 0;

	SecurityEvent event;

	String es = NULL;


	/*
	 * Compute the number of elements in the list and send it to
	 * the client.
	 */
	cnt = Model->trajectory_size(Model);

	cmdbufr->reset(cmdbufr);
	cmdbufr->add(cmdbufr, (unsigned char *) &cnt, sizeof(cnt));
	if ( !mgmt->send_packet(mgmt, PossumPipe_data, cmdbufr) )
		ERR(goto done);
	if ( Debug )
		fprintf(stderr, "Sent trajectory size: %zu\n", cnt);


	/* Send each trajectory point. */
	INIT(HurdLib, String, es, ERR(goto done));

	Model->rewind_event(Model);

	for (lp= 0; lp < cnt; ++lp ) {
		if ( !Model->get_event(Model, &event) )
			ERR(goto done);
		if ( event == NULL )
			continue;
		if ( !event->format(event, es) )
			ERR(goto done);

		cmdbufr->reset(cmdbufr);
		cmdbufr->add(cmdbufr, (unsigned char *) es->get(es), \
			     es->size(es) + 1);
		if ( !mgmt->send_packet(mgmt, PossumPipe_data, cmdbufr) )
			ERR(goto done);
		es->reset(es);
	}

	retn = true;

 done:
	WHACK(es);

	return retn;
}


/**
 * Private function.
 *
 * This function is responsible for returning the current forensics
 * list to the caller.  The protocol used is to send the number of
 * elements in the list followed by each point in the forensics
 * path as an ASCII string.
 *
 * \param mgmt		The object used to communicate with the canister
 *			management instance.
 *
 * \param cmdbufr	The object which will be used to hold the
 *			information which will be transmitted.
 *
 * \return		A boolean value is returned to indicate whether
 *			or not the command was processed.  A false value
 *			indicates the processing of commands should be
 *			terminated while a true value indicates an
 *			additional command cycle should be processed.
 */

static _Bool send_forensics(CO(PossumPipe, mgmt), CO(Buffer, cmdbufr))

{
	_Bool retn = false;

	size_t lp,
	       cnt = 0;

	SecurityEvent event;

	String es = NULL;


	/*
	 * Compute the number of elements in the list and send it to
	 * the client.
	 */
	cnt = Model->forensics_size(Model);

	cmdbufr->reset(cmdbufr);
	cmdbufr->add(cmdbufr, (unsigned char *) &cnt, sizeof(cnt));
	if ( !mgmt->send_packet(mgmt, PossumPipe_data, cmdbufr) )
		ERR(goto done);
	if ( Debug )
		fprintf(stderr, "Sent forensics size: %zu\n", cnt);


	/* Send each trajectory point. */
	INIT(HurdLib, String, es, ERR(goto done));

	Model->rewind_forensics(Model);
	for (lp= 0; lp < cnt; ++lp ) {
		if ( !Model->get_forensics(Model, &event) )
			ERR(goto done);
		if ( event == NULL )
			continue;
		if ( !event->format(event, es) )
			ERR(goto done);

		cmdbufr->reset(cmdbufr);
		cmdbufr->add(cmdbufr, (unsigned char *) es->get(es), \
			     es->size(es) + 1);
		if ( !mgmt->send_packet(mgmt, PossumPipe_data, cmdbufr) )
			ERR(goto done);
		es->reset(es);
	}

	retn = true;

 done:
	WHACK(es);

	return retn;
}


/**
 * Private function.
 *
 * This function is responsible for returning the current behavioral
 * map to the caller.  The protocol used is to send the number of
 * elements in the map followed by each point in the map as a hexadecimal
 * ASCII string.
 *
 * \param mgmt		The socket object used to communicate with
 *			the canister management instance.
 *
 * \param cmdbufr	The object which will be used to hold the
 *			information which will be transmitted.
 *
 * \return		A boolean value is returned to indicate whether
 *			or not the command was processed.  A false value
 *			indicates the processing of commands should be
 *			terminated while a true value indicates an
 *			additional command cycle should be processed.
 */

static _Bool send_contours(CO(PossumPipe, mgmt), CO(Buffer, cmdbufr))

{
	_Bool retn = false;

	uint8_t *p,
		 pi;

	char point[NAAAIM_IDSIZE * 2 + 1];

	size_t lp,
	       cnt = 0;

	SecurityPoint cp = NULL;


	/*
	 * Compute the number of elements in the list and send it to
	 * the client.
	 */
	cnt = Model->points_size(Model);

	cmdbufr->reset(cmdbufr);
	cmdbufr->add(cmdbufr, (unsigned char *) &cnt, sizeof(cnt));
	if ( !mgmt->send_packet(mgmt, PossumPipe_data, cmdbufr) )
		ERR(goto done);
	if ( Debug )
		fprintf(stderr, "Sent contour size: %zu\n", cnt);


	/* Send each trajectory point. */
	Model->rewind_points(Model);
	for (lp= 0; lp < cnt; ++lp ) {
		if ( !Model->get_point(Model, &cp) )
			ERR(goto done);
		if ( cp == NULL )
			continue;

		memset(point, '\0', sizeof(point));
		p = cp->get(cp);
		for (pi= 0; pi < NAAAIM_IDSIZE; ++pi)
			snprintf(&point[pi*2], 3, "%02x", *p++);

		cmdbufr->reset(cmdbufr);
		cmdbufr->add(cmdbufr, (unsigned char *) point, sizeof(point));
		if ( !mgmt->send_packet(mgmt, PossumPipe_data, cmdbufr) )
			ERR(goto done);
	}

	retn = true;


 done:

	return retn;
}


/**
 * Private function.
 *
 * This function is responsible for returning the current set of
 * AI events to the caller.  The protocol used is to send the number of
 * elements in the event list followed by each event as an ASCII string.
 *
 * \param mgmt		The socket object used to communicate with
 *			the canister management instance.
 *
 * \param cmdbufr	The object which will be used to hold the
 *			information which will be transmitted.
 *
 * \return		A boolean value is returned to indicate whether
 *			or not the command was processed.  A false value
 *			indicates an error was encountered while sending
 *			the event list while a true value indicates the
 *			event list was succesfully sent.
 */

static _Bool send_events(CO(PossumPipe, mgmt), CO(Buffer, cmdbufr))

{
	_Bool retn = false;

	size_t lp,
	       cnt = 0;

	String event = NULL;


	/*
	 * Compute the number of elements in the AI list and send it to
	 * the client.
	 */
	cnt = Model->TSEM_events_size(Model);

	cmdbufr->reset(cmdbufr);
	cmdbufr->add(cmdbufr, (unsigned char *) &cnt, sizeof(cnt));
	if ( !mgmt->send_packet(mgmt, PossumPipe_data, cmdbufr) )
		ERR(goto done);
	if ( Debug )
		fprintf(stderr, "Sent event size: %zu\n", cnt);


	/* Send each event. */
	Model->TSEM_rewind_event(Model);
	for (lp= 0; lp < cnt; ++lp) {
		if ( !Model->get_TSEM_event(Model, &event) )
			ERR(goto done);
		if ( event == NULL )
			continue;

		cmdbufr->reset(cmdbufr);
		cmdbufr->add(cmdbufr, (unsigned char *) event->get(event), \
			     event->size(event));
		if ( !mgmt->send_packet(mgmt, PossumPipe_data, cmdbufr) )
			ERR(goto done);
	}

	retn = true;


 done:
	return retn;
}


/**
 * Private function.
 *
 * This function implements the processing of a command from the
 * canister management utility.  This command comes in the form
 * of a binary encoding of the desired command to be run.
 *
 * \param mgmt		The object used to communicate with the canister
 *			management instance.
 *
 * \param cmdbufr	The object containing the command to be
 *			processed.
 *
 * \return		A boolean value is returned to indicate whether
 *			or not the command was processed.  A false value
 *			indicates the processing of commands should be
 *			terminated while a true value indicates an
 *			additional command cycle should be processed.
 */

static _Bool process_command(CO(PossumPipe, mgmt), CO(Buffer, cmdbufr))

{
	_Bool retn = false;

	int *cp;


	if ( cmdbufr->size(cmdbufr) != sizeof(int) )
		ERR(goto done);

	cp = (int *) cmdbufr->get(cmdbufr);
	switch ( *cp ) {
		case show_measurement:
			cmdbufr->reset(cmdbufr);
			if ( !Model->get_measurement(Model, cmdbufr) )
				ERR(goto done);

			if ( !mgmt->send_packet(mgmt, PossumPipe_data, \
						cmdbufr) )
				ERR(goto done);
			retn = true;
			break;

		case show_trajectory:
			retn = send_trajectory(mgmt, cmdbufr);
			break;

		case show_forensics:
			retn = send_forensics(mgmt, cmdbufr);
			break;

		case show_contours:
			retn = send_contours(mgmt, cmdbufr);
			break;

		case show_events:
			retn = send_events(mgmt, cmdbufr);
			break;
	}


 done:
	return retn;
}


/**
 * Global function.
 *
 * The following function implements a function for returning something
 * that approximates monotonic time for the enclave.  The expection
 * is for an ECALL to set the Current_Time variable to some initial
 * value, typically when the ECAL was made.  Each time this function
 * is called the value is incremented so a value which roughly
 * approximately monotonic time is available.
 *
 * For the purposes of a PossumPiple this is sufficient since the
 * replay defense is based on the notion that an endpoint will never
 * see an OTEDKS key repeated.
 *
 * \param timeptr	If this value is non-NULL the current time
 *			value is copied into the location specified by
 *			this pointer.
 *
 * \return		The current value of the enclave time variable
 *			is returned to the caller.
 */

time_t time(time_t *timeptr)

{
	if ( timeptr != NULL )
		*timeptr = Current_Time;

	return Current_Time++;
}


/**
 * Private function.
 *
 * The following function is a wrapper function for one invocation of
 * a management session.  This function is designed to be called by
 * the management call and provides a wrapper for the creation and
 * execution of a single PossumPipe connection.
 *
 * \param port		The port number which the PossumPipe is to
 *			listen on.
 *
 * \param spid		The object containing the binary representation
 *			of the service provider id.
 *
 * \return		No return value is currently defined.
 */

static void run_session(int port, CO(Buffer, spid))

{
	_Bool done = false;

	PossumPipe_type pipe_retn;

	Buffer bufr = NULL;

	PossumPipe pipe = NULL;


	INIT(NAAAIM, PossumPipe, pipe, ERR(goto done));
	if ( Debug )
		pipe->debug(pipe, Debug);

	if ( !pipe->init_server(pipe, NULL, port, false) )
		ERR(goto done);


	/* Receive and process host commands. */
	INIT(HurdLib, Buffer, bufr, ERR(goto done));

	while ( 1 ) {
		if ( !pipe->accept_connection(pipe) ) {
			fputs("Error accepting connection.\n", stderr);
			ERR(goto done);
		}
		if ( !pipe->start_host_mode(pipe, spid) ) {
			fputs("Error receiving data.\n", stderr);
			goto done;
		}

		while ( !done ) {
			pipe_retn = pipe->receive_packet(pipe, bufr);
			if ( pipe_retn == PossumPipe_eop )
				done = true;
			if ( pipe_retn == PossumPipe_failure )
				ERR(goto done);
			if ( pipe_retn == PossumPipe_data ) {
				if ( !process_command(pipe, bufr) )
					ERR(goto done);
			}
			bufr->reset(bufr);
		}

		done = false;
		pipe->reset(pipe);
	}


 done:
	WHACK(bufr);
	WHACK(pipe);

	return;
}


/**
 * ECALL 10
 *
 * This function implements the ECALL entry point for the ISOidentity
 * management interface.
 *
 * \param ecall10	A pointer to the interface structure for the
 *			manager ECALL.
 *
 * \return	A boolean value is used to indicate the status of
 *		the management interface.  A false value indicates an
 *		error was encountered while setting up or operating
 *		the interface.  A true value indicates the management
 *		interface had executed successfully.
 */

_Bool manager(struct ISOidentity_ecall10_interface *ecall10)

{
	_Bool retn = false;

	Buffer spid = NULL;


	/* Set the debug status. */
	Debug = ecall10->debug;

	/* Initialize the time. */
	Current_Time = ecall10->current_time;

	/* Convert the SPID value into binary form. */
	INIT(HurdLib, Buffer, spid, ERR(goto done));
	if ( !spid->add_hexstring(spid, ecall10->spid) )
		ERR(goto done);


	/* Stash the identity token. */
	Identity      = ecall10->identity;
	Identity_size = ecall10->identity_size;


	/* Start the management interface. */
	if ( Debug )
		fprintf(stdout, "ISOidentity manager: port=%d\n", \
			ecall10->port);

	while ( 1 ) {
		run_session(ecall10->port, spid);
	}


 done:
	WHACK(spid);

	return retn;
}


/**
 * ECALL 11
 *
 * This function implements the ecall entry point for a function which
 * generates the platform specific device identity.
 *
 * \param id	A pointer containing the buffer which will be loaded
 *		with the 32 byte platform specific enclave identity.
 *
 * \return	A boolean value is used to indicate the status of the
 *		identity generation.  A false value indicates an error
 *		was encountered while a true value indicates the
 *		identity was successfully generated.
 */

_Bool generate_identity(uint8_t *id)

{
	_Bool retn = false;

	int rc;

	uint8_t keydata[16] __attribute__((aligned(128)));

	char report_data[64] __attribute__((aligned(128)));

	Buffer b,
	       bufr = NULL;

	Sha256 sha256 = NULL;

	struct SGX_report __attribute__((aligned(512))) report;

	struct SGX_targetinfo target;

	struct SGX_keyrequest keyrequest;


	/* Request a self report to get the measurement. */
	memset(&target, '\0', sizeof(struct SGX_targetinfo));
	memset(&report, '\0', sizeof(struct SGX_report));
	memset(report_data, '\0', sizeof(report_data));
	enclu_ereport(&target, &report, report_data);


	/* Request the key. */
	memset(keydata, '\0', sizeof(keydata));
	memset(&keyrequest, '\0', sizeof(struct SGX_keyrequest));

	keyrequest.keyname   = SRDE_KEYSELECT_SEAL;
	keyrequest.keypolicy = SRDE_KEYPOLICY_SIGNER;
	memcpy(keyrequest.keyid, report.body.mr_enclave.m, \
	       sizeof(keyrequest.keyid));


	/* Generate the derived key and return it to the caller. */
	if ( (rc = enclu_egetkey(&keyrequest, keydata)) != 0 ) {
		fprintf(stdout, "EGETKEY return: %d\n", rc);
		goto done;
	}

	INIT(HurdLib, Buffer, bufr, ERR(goto done));
	if ( !bufr->add(bufr, keydata, sizeof(keydata)) )
		ERR(goto done);

	INIT(NAAAIM, Sha256, sha256, ERR(goto done));
	sha256->add(sha256, bufr);
	if ( !sha256->compute(sha256) )
		ERR(goto done);

	b = sha256->get_Buffer(sha256);
	memcpy(id, b->get(b), 32);

	retn = true;


 done:
	WHACK(bufr);
	WHACK(sha256);

	return retn;
}


/**
 * ECALL 13
 *
 * This function implements the ecall entry point for a function which
 * adds an identity verifier to the list of valid POSSUM communication
 * parties.
 *
 * \param ecall13	A pointer to the input structure to the ECALL.
 *
 * \return	A boolean value is used to indicate the status of the
 *		registration of the identity verifier.  A false value
 *		indicates an error was encountered while registering
 *		the verifier while a true value indicates the verifier
 *		was successfully registered.
 */

_Bool add_verifier(struct ISOidentity_ecall13 *ecall13)

{
	_Bool retn = false;

	Buffer bufr = NULL;

	Ivy ivy = NULL;


	/* Decode the raw Ivy buffer. */
	INIT(HurdLib, Buffer, bufr, ERR(goto done));
	INIT(NAAAIM, Ivy, ivy, ERR(goto done));

	if ( !bufr->add(bufr, ecall13->verifier, ecall13->verifier_size) )
		ERR(goto done);
	if ( !ivy->decode(ivy, bufr) )
		ERR(goto done);


	/* Add the Ivy object to the verifier list. */
	if ( Verifiers == NULL )
		INIT(HurdLib, Buffer, Verifiers, ERR(goto done));

	if ( !Verifiers->add(Verifiers, (unsigned char *) &ivy, sizeof(Ivy)) )
		ERR(goto done);
	retn = true;


 done:
	if ( !retn )
		WHACK(ivy);
	WHACK(bufr);

	return retn;
}

/** \file
 *
 * This file implements a utility for running and managing software
 * stacks in an independent process based masurement domain.  After
 * creating a independent measurement domain the utility forks and
 * then executes the boot of the cartridge in a subordinate process.
 * The parent process monitors the following file:
 *
 * /sys/fs/integrity-events/update-NS
 *
 * Where NS is the inode number of the security event namespace that
 * is being monitored.
 *
 * The measurement update events are transmitted to a process running
 * a security event modeling instance that has the CAP_TRUST capability
 * set that allows the process to modify the trust status of a
 * namespace.  This process makes a decision as to whether or not
 * a process is relegated to 'bad actor' status based on feedback
 * from the mode.
 *
 * For measurement domain updates.
 *
 * The domain is managed through a UNIX domain socket which is created
 * in the following location:
 *
 * /var/run/quixote-process.PIDNUM
 */

/**************************************************************************
 * Copyright (c) 2020, Enjellic Systems Development, LLC. All rights reserved.
 **************************************************************************/

#define CARTRIDGE_DIRECTORY "/var/run/Cartridges"

#define SYSFS_UPDATES  "/sys/fs/integrity-events/update-%u"
#define SYSFS_EXTERNAL "/sys/kernel/security/integrity/events/external"

#define READ_SIDE  0
#define WRITE_SIDE 1

#define CLONE_EVENTS 0x00000040

#define CAP_TRUST 38

#define SYS_CONFIG_DOMAIN  436
#define IMA_TE_ENFORCE	   0x8
#define IMA_EVENT_EXTERNAL 0x10

#define SYS_CONFIG_ACTOR  437
#define DISCIPLINE_ACTOR  1
#define RELEASE_ACTOR	  2

#define _GNU_SOURCE

#define GWHACK(type, var) {			\
	size_t i=var->size(var) / sizeof(type);	\
	type *o=(type *) var->get(var);		\
	while ( i-- ) {				\
		(*o)->whack((*o));		\
		o+=1;				\
	}					\
}


/* Include files. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <limits.h>
#include <sched.h>
#include <glob.h>
#include <sys/capability.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/socket.h>
#include <linux/un.h>

#include <HurdLib.h>
#include <Buffer.h>
#include <String.h>
#include <File.h>

#include "sancho-cmd.h"

#include "NAAAIM.h"
#include "TTYduct.h"
#include "LocalDuct.h"
#include "SHA256.h"

#include "ContourPoint.h"
#include "ExchangeEvent.h"
#include "ISOidentity.h"


/**
 * Variable used to indicate that debugging is enabled and to provide
 * the filehandle to be used for debugging.
 */
static FILE *Debug = NULL;

/**
 * The process id of the cartridge monitor process.
 */
static pid_t Monitor_pid;

/**
 * The modeling object for the canister.
 */
static ISOidentity Model = NULL;

/**
 * The seal status of the domain.  This variable is set by a
 * seal event for the domain.  Updates which are not in the
 * security state will cause disciplining requests to be generated
 * for the process initiating the event.
 */
static _Bool Sealed = false;

/**
 * The following variable holds the current measurement.
 */
#if 0
static unsigned char Measurement[32];
#endif

/**
 * The following variable holds booleans which describe signals
 * which were received.
 */
struct {
	_Bool sigint;
	_Bool sigterm;
	_Bool sighup;
	_Bool sigquit;
	_Bool stop;

	_Bool sigchild;
} Signals;


/**
 * System call wrapper for setting the security state of a process.
 */
static inline int sys_config_actor(pid_t pid, unsigned long flags)
{
	return syscall(SYS_CONFIG_ACTOR, pid, flags);
}

/**
 * System call wrapper for configuring a security event domain.
 */
static inline int sys_config_domain(unsigned char *bufr, size_t cnt, \
				    unsigned long flags)
{
	return syscall(SYS_CONFIG_DOMAIN, bufr, cnt, flags);
}


/**
 * Private function.
 *
 * This function implements the signal handler for the utility.  It
 * sets the signal type in the Signals structure.
 *
 * \param signal	The number of the signal which caused the
 *			handler to execute.
 */

void signal_handler(int signal, siginfo_t *siginfo, void *private)

{
	if ( Debug )
		fprintf(Debug, "%s(%d): signal = %d\n", __func__, getpid(), \
			signal);

	switch ( signal ) {
		case SIGINT:
			Signals.stop = true;
			return;
		case SIGTERM:
			Signals.stop = true;
			return;
		case SIGHUP:
			Signals.stop = true;
			return;
		case SIGQUIT:
			Signals.stop = true;
			return;
		case SIGCHLD:
			Signals.sigchild = true;
			return;
	}

	return;
}


/**
 * Private function.
 *
 * This function implements checking for whether or not the cartridge
 * process has terminated.
 *
 * \param cartridge_pid	The pid of the cartridge.
 *
 *
 * \return		A boolean value is used to indicate whether
 *			or not the designed process has exited.  A
 *			false value indicates it has not while a
 *			true value indicates it has.
 */

static _Bool child_exited(const pid_t cartridge)

{
	int status;


	if ( waitpid(cartridge, &status, WNOHANG) != cartridge )
		return false;

	return true;
}


#if 0
/**
 * Private function.
 *
 * This function carries out the addition of a measurement value
 * generated by the kernel to the current measurement state of the
 * security domain.
 *
 * \param bufr		A pointer to the character buffer containing
 *			the hexadecimally encoded measurement from
 *			the domain.
 *
 * \return		A boolean value is returned to indicate whether
 *			or not addition of the measurement succeeded.  A
 *			false value indicates the addition of the
 *			measurement failed while a true value indicates
 *			the measurement had succeeded.
 */

static _Bool add_measurement(CO(char *, bufr))

{
	_Bool retn = false;

	Buffer bf,
	       input = NULL;

	Sha256 sha256 = NULL;


	/* Convert the ASCII measurement into a binary buffer. */
	INIT(HurdLib, Buffer, input, ERR(goto done));
	if ( !input->add_hexstring(input, bufr) )
		ERR(goto done);


	/* Update the internal measurement. */
	INIT(NAAAIM, Sha256, sha256, ERR(goto done));

	sha256->add(sha256, input);
	if ( !sha256->compute(sha256) )
		ERR(goto done);
	bf = sha256->get_Buffer(sha256);

	input->reset(input);
	input->add(input, Measurement, sizeof(Measurement));
	input->add_Buffer(input, bf);

	sha256->reset(sha256);
	sha256->add(sha256, input);
	if ( !sha256->compute(sha256) )
		ERR(goto done);

	memcpy(Measurement, bf->get(bf), bf->size(bf));
	if ( Debug )
		fprintf(Debug, "Add measurement: %s\n", bufr);

	retn = true;


 done:
	WHACK(input);
	WHACK(sha256);

	return retn;
}
#endif


/**
 * Private function.
 *
 * This function carries out the addition of a state value to the
 * current security model.
 *
 * \param bufr		A pointer to the character buffer containing
 *			the hexadecimally encoded state value.
 *
 * \return		A boolean value is returned to indicate whether
 *			or not addition of the state value succeeded.  A
 *			false value indicates the addition of the
 *			state failed while a true value indicates
 *			the state injection had succeeded.
 */

static _Bool add_state(CO(char *, inbufr))

{
	_Bool retn = false;

	Buffer bufr = NULL;


	/* Convert the ASCII encoded state to a binary value. */
	INIT(HurdLib, Buffer, bufr, ERR(goto done));

	if ( !bufr->add_hexstring(bufr, inbufr) )
		ERR(goto done);
	if ( bufr->size(bufr) != NAAAIM_IDSIZE )
		ERR(goto done);


	/* Add the state to the model. */
	if ( !Model->update_map(Model, bufr) )
			ERR(goto done);
	retn = true;


 done:
	WHACK(bufr);

	return retn;
}


/**
 * Private function.
 *
 * This function carries out the addition of a security state event
 * to the current security state model.
 *
 * \param bufr		A pointer to the character buffer containing
 *			the ASCII encoded state description.
 *
 * \return		A boolean value is returned to indicate whether
 *			or not addition of the event succeeded.  A
 *			false value indicates the addition failed while
 *			a true value indicates the addition succeeded.
 */

static _Bool add_event(CO(char *, inbufr))

{
	_Bool status,
	      discipline,
	      retn = false;

	pid_t pid;

	String update = NULL;

	ExchangeEvent event = NULL;


	INIT(HurdLib, String, update, ERR(goto done));
	if ( !update->add(update, inbufr) )
		ERR(goto done);

	INIT(NAAAIM, ExchangeEvent, event, ERR(goto done));
	if ( !event->parse(event, update) )
		ERR(goto done);
	if ( !event->measure(event) )
		ERR(goto done);
	if ( !Model->update(Model, event, &status, &discipline) )
		ERR(goto done);

	Model->discipline_pid(Model, &pid);

	if ( Debug )
		fprintf(Debug, "Model update: status=%d, discipline=%d\n",
			status, discipline);


	/* Security domain is not being disciplined, release the process. */
	if ( !Sealed ) {
		if ( Debug )
			fputs("Unsealed, releasing actor.\n", Debug);
		if ( sys_config_actor(pid, RELEASE_ACTOR) < 0 )
			fprintf(stderr, "[%s]: Release actor status: %d:%s\n",
				__func__, errno, strerror(errno));
	}


	/*
	 * Security domain is being disciplined.  Release processes
	 * that are not in the event map as bad actors and others as
	 * good actors.
	 */
	if ( Sealed ) {
		if ( discipline ) {
			if ( Debug )
				fputs("Sealed, releasing bad actor.\n", Debug);
			if ( sys_config_actor(pid, DISCIPLINE_ACTOR) < 0 ) {
				fprintf(stderr, "Bad actor release error: "  \
					"%d:%s\n", errno, strerror(errno));
					retn = false;
					goto done;
			}
		} else {
			if ( Debug )
				fputs("Sealed, releasing actor.\n", Debug);
			if ( sys_config_actor(pid, RELEASE_ACTOR)  < 0 ) {
				fprintf(stderr, "Good actor release error: "  \
					"%d:%s\n", errno, strerror(errno));
					retn = false;
					goto done;
			}
		}
	}

	retn = true;


 done:
	if ( !status )
		WHACK(event);

	WHACK(update);

	return retn;
}


/**
 * Private function.
 *
 * This function carries out the addition of the hardware aggregate
 * measurement to the current security state model.
 *
 * \param bufr		A pointer to the character buffer containing
 *			the ASCII hardware aggregate measurement.
 *
 * \return		A boolean value is returned to indicate whether
 *			or addition of the aggregate value succeeded.  A
 *			false value indicates the addition failed while
 *			a true value indicates the addition succeeded.
 */

static _Bool add_aggregate(CO(char *, inbufr))

{
	_Bool retn = false;

	Buffer aggregate = NULL;


	if ( Debug )
		fprintf(Debug, "aggregate %s", inbufr);

	INIT(HurdLib, Buffer, aggregate, ERR(goto done));
	if ( !aggregate->add_hexstring(aggregate, inbufr) )
		ERR(goto done);

	if ( !Model->set_aggregate(Model, aggregate) )
		ERR(goto done);

	retn = true;


 done:
	WHACK(aggregate);

	return retn;
}


/**
 * Private function.
 *
 * This function carries out the addition of a description of an
 * intercepted LSM event to the current security domain model.
 *
 * \param TE_event	A pointer to the character buffer containing
 *			the ASCII encoded event.
 *
 * \return		A boolean value is returned to indicate whether
 *			or addition of the event.  A false value indicates
 *			the addition failed while a true value indicates
 *			the addition succeeded.
 */

static _Bool add_TE_event(CO(char *, TE_event))

{
	_Bool retn = false;

	String event = NULL;


	INIT(HurdLib, String, event, ERR(goto done));
	event->add(event, TE_event);
	if ( !Model->add_ai_event(Model, event) )
		ERR(goto done);

	retn = true;


 done:

	return retn;
}


/**
 * Private function.
 *
 * This function is responsible for the processing of Turing security
 * state events generated by the kernel.
 *
 * \param bufr		A pointer to the character buffer containing
 *			the ASCII encoded event.
 *
 * \return		A boolean value is returned to indicate whether
 *			or not processing of the event was successful.  A
 *			false value indicates a failure in event
 *			processing while a true value indicates that
 *			event processing has succeeded.
 */

static _Bool process_event(const char *event)

{
	_Bool retn = false;

	const char *event_arg;

	struct sancho_cmd_definition *cp;

	static const char *measurement = "measurement " ;


	/* Locate the event type. */
	if ( strncmp(event, measurement, strlen(measurement)) == 0 ) {
		retn = true;
		goto done;
	}

	for (cp= Sancho_cmd_list; cp->syntax != NULL; ++cp) {
		if ( strncmp(cp->syntax, event, strlen(cp->syntax)) == 0 )
			break;
	}

	if ( cp->syntax == NULL ) {
		fprintf(stderr, "Unknown event: %s\n", event);
		goto done;
	}

	event_arg = event + strlen(cp->syntax);


	/* Dispatch the event. */
	switch ( cp->command ) {
#if 0
		case measurement_event:
			retn = add_measurement(p);
			break;
#endif

		case sancho_state:
			retn = add_state(event_arg);
			break;

		case exchange_event:
			retn = add_event(event_arg);
			break;

		case aggregate_event:
			retn = add_aggregate(event_arg);
			break;

		case seal_event:
			if ( Debug )
				fputs("Kernel sealed domain.\n", Debug);

			Model->seal(Model);
			retn   = true;
			Sealed = true;
			break;

		case ai_event:
			retn = add_TE_event(event_arg);
			break;

		default:
			fprintf(stderr, "Unknown event: %s\n", event);
			break;
	}

	retn = true;


 done:
	return retn;
}


#if 0
/**
 * Private function.
 *
 * This function is responsible for returning a list from the
 * co-processor to a management client.
 *
 * \param mgmt	The socket object used to communicate with
 *		the cartridge management instance.
 *
 * \param bufr	The object which will be used to hold the
 *			information which will be transmitted.
 *
 * \return		A boolean value is returned to indicate whether
 *			or not the command was processed.  A false value
 *			indicates the processing of commands should be
 *			terminated while a true value indicates an
 *			additional command cycle should be processed.
 */

static _Bool send_list(CO(TTYduct, duct), CO(LocalDuct, mgmt), \
		       CO(Buffer, bufr), CO(char *, cmd))

{
	_Bool retn = false;

	uint32_t cnt;


	/* Send the specified listing command. */
	bufr->reset(bufr);
	if ( !bufr->add(bufr, (unsigned char *) cmd, strlen(cmd)) )
		ERR(goto done);
	if ( !duct->send_Buffer(duct, bufr) )
		ERR(goto done);


	/* Return the result stream to the client. */
	bufr->reset(bufr);
	if ( !duct->receive_Buffer(duct, bufr) )
		ERR(goto done);

	cnt = *(unsigned int *) bufr->get(bufr);
	if ( !mgmt->send_Buffer(mgmt, bufr) )
		ERR(goto done);

	while ( cnt-- > 0 ) {
		bufr->reset(bufr);
		if ( !duct->receive_Buffer(duct, bufr) )
			ERR(goto done);
		if ( !mgmt->send_Buffer(mgmt, bufr) )
			ERR(goto done);
	}

	retn = true;

 done:
	return retn;
}
#endif


/**
 * Private function.
 *
 * This function is responsible for returning the current security event
 * trajectory list to the caller.  The protocol used is to send the number
 * of elements in the list followed by each point as an ASCII string.
 *
 * \param mgmt		The socket object used to communicate with
 *			the quixote-console management instance.
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

static _Bool send_trajectory(CO(LocalDuct, mgmt), CO(Buffer, cmdbufr))

{
	_Bool retn = false;

	size_t lp,
	       cnt = 0;

	ExchangeEvent event;

	String es = NULL;


	/*
	 * Compute the number of elements in the list and send it to
	 * the client.
	 */
	cnt = Model->size(Model);

	cmdbufr->reset(cmdbufr);
	cmdbufr->add(cmdbufr, (unsigned char *) &cnt, sizeof(cnt));
	if ( !mgmt->send_Buffer(mgmt, cmdbufr) )
		ERR(goto done);
	if ( Debug )
		fprintf(Debug, "Sent trajectory size: %zu\n", cnt);


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
		if ( !mgmt->send_Buffer(mgmt, cmdbufr) )
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
 * \param mgmt		The socket object used to communicate with
 *			the the quixote-console management instance.
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

static _Bool send_forensics(CO(LocalDuct, mgmt), CO(Buffer, cmdbufr))

{
	_Bool retn = false;

	size_t lp,
	       cnt = 0;

	ExchangeEvent event;

	String es = NULL;


	/*
	 * Compute the number of elements in the list and send it to
	 * the client.
	 */
	cnt = Model->forensics_size(Model);

	cmdbufr->reset(cmdbufr);
	cmdbufr->add(cmdbufr, (unsigned char *) &cnt, sizeof(cnt));
	if ( !mgmt->send_Buffer(mgmt, cmdbufr) )
		ERR(goto done);
	if ( Debug )
		fprintf(Debug, "Sent forensics size: %zu\n", cnt);


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

		/*
		 * The following is a safety check to make sure that
		 * the object event is populated in case there was
		 * an error such as a failure to reset the cursor
		 * between trajectory or forensics traversals.
		 */
		if ( es->size(es) == 0 ) {
			if ( !es->add(es, "Unknown event.") )
				ERR(goto done);
		}

		/* Send the contents of the string object. */
		cmdbufr->reset(cmdbufr);
		cmdbufr->add(cmdbufr, (unsigned char *) es->get(es), \
			     es->size(es) + 1);
		if ( !mgmt->send_Buffer(mgmt, cmdbufr) )
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
 * This function is responsible for returning the current security states
 * to the caller.  The protocol used is to send the number of elements in
 * the map followed by each state in the model as a hexadecimal ASCII
 * string.
 *
 * \param mgmt		The socket object used to communicate with
 *			the quixote-console management instance.
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

static _Bool send_states(CO(LocalDuct, mgmt), CO(Buffer, cmdbufr))

{
	_Bool retn = false;

	uint8_t *p,
		 pi;

	char point[NAAAIM_IDSIZE * 2 + 1];

	size_t lp,
	       cnt = 0;

	ContourPoint cp = NULL;


	/*
	 * Compute the number of elements in the list and send it to
	 * the client.
	 */
	cnt = Model->contours_size(Model);

	cmdbufr->reset(cmdbufr);
	cmdbufr->add(cmdbufr, (unsigned char *) &cnt, sizeof(cnt));
	if ( !mgmt->send_Buffer(mgmt, cmdbufr) )
		ERR(goto done);
	if ( Debug )
		fprintf(Debug, "Sent contour size: %zu\n", cnt);


	/* Send each trajectory point. */
	Model->rewind_contours(Model);

	for (lp= 0; lp < cnt; ++lp ) {
		if ( !Model->get_contour(Model, &cp) )
			ERR(goto done);
		if ( cp == NULL )
			continue;

		memset(point, '\0', sizeof(point));
		p = cp->get(cp);
		for (pi= 0; pi < NAAAIM_IDSIZE; ++pi)
			snprintf(&point[pi*2], 3, "%02x", *p++);

		cmdbufr->reset(cmdbufr);
		cmdbufr->add(cmdbufr, (unsigned char *) point, sizeof(point));
		if ( !mgmt->send_Buffer(mgmt, cmdbufr) )
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
 * TE violations to the caller.  The protocol used is to send the number of
 * elements in the event list followed by each event as an ASCII string.
 *
 * \param mgmt		The socket object used to communicate with
 *			the quixote-console management instance.
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

static _Bool send_events(CO(LocalDuct, mgmt), CO(Buffer, cmdbufr))

{
	_Bool retn = false;

	size_t lp,
	       cnt = 0;

	String event = NULL;


	/*
	 * Compute the number of elements in the AI list and send it to
	 * the client.
	 */
	cnt = Model->ai_events_size(Model);

	cmdbufr->reset(cmdbufr);
	cmdbufr->add(cmdbufr, (unsigned char *) &cnt, sizeof(cnt));
	if ( !mgmt->send_Buffer(mgmt, cmdbufr) )
		ERR(goto done);
	if ( Debug )
		fprintf(Debug, "Sent event size: %zu\n", cnt);


	/* Send each event. */
	Model->ai_rewind_event(Model);

	for (lp= 0; lp < cnt; ++lp) {
		if ( !Model->get_ai_event(Model, &event) )
			ERR(goto done);
		if ( event == NULL )
			continue;

		cmdbufr->reset(cmdbufr);
		cmdbufr->add(cmdbufr, (unsigned char *) event->get(event), \
			     event->size(event));
		if ( !mgmt->send_Buffer(mgmt, cmdbufr) )
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
 * quixote-console utility.
 *
 * \param mgmt		The socket object used to communicate with
 *			the security domain management instance.
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

static _Bool process_command(CO(LocalDuct, mgmt), CO(Buffer, cmdbufr))

{
	_Bool retn = false;

	static unsigned char ok[] = "OK";

	int *cp;


	if ( cmdbufr->size(cmdbufr) != sizeof(int) )
		ERR(goto done);

	cp = (int *) cmdbufr->get(cmdbufr);
	switch ( *cp ) {
		case show_measurement:
			cmdbufr->reset(cmdbufr);
			if ( !Model->get_measurement(Model, cmdbufr) )
					ERR(goto done);
			if ( !mgmt->send_Buffer(mgmt, cmdbufr) )
				ERR(goto done);
			retn = true;
			break;

		case show_state:
			cmdbufr->reset(cmdbufr);
			if ( !Model->get_state(Model, cmdbufr) )
				ERR(goto done);

			if ( !mgmt->send_Buffer(mgmt, cmdbufr) )
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
			retn = send_states(mgmt, cmdbufr);
			break;

		case show_events:
			retn = send_events(mgmt, cmdbufr);
			break;

		case seal_event:
			Sealed = true;
			Model->seal(Model);

			cmdbufr->reset(cmdbufr);
			if ( !cmdbufr->add(cmdbufr, ok, sizeof(ok)) )
				ERR(goto done);
			if ( !mgmt->send_Buffer(mgmt, cmdbufr) )
				ERR(goto done);

			retn = true;
			break;
	}


 done:
	return retn;
}


/**
 * Private function.
 *
 * This function implements the initialization of a behavioral map
 * for the cartridge being executed.
 *
 * \param mapfile	The name of the file containing the behavioral
 *			model.  The model is expected to consist of
 *			model events.
 *
 * \return		A boolean value is returned to indicate whether
 *			or not the command was processed.  A false value
 *			indicates the processing of commands should be
 *			terminated while a true value indicates an
 *			additional command cycle should be processed.
 */

static _Bool initialize_state(char *mapfile)

{
	_Bool retn = false;

	char *p,
	     inbufr[256];

	FILE *bmap = NULL;


	/* Open the behavioral map and initialize the binary point object. */
	if ( (bmap = fopen(mapfile, "r")) == NULL )
		ERR(goto done);


	/* Loop over the mapfile. */
	while ( fgets(inbufr, sizeof(inbufr), bmap) != NULL ) {
		if ( (p = strchr(inbufr, '\n')) != 0 )
			*p = '\0';

		if ( Debug )
			fprintf(Debug, "Initialize: %s\n", inbufr);

		if ( !process_event(inbufr) )
			ERR(goto done);
	}

	retn = true;


 done:
	return retn;
}


/**
 * Private function.
 *
 * This function sets up a namespace and returns a file descriptor
 * to the caller which references the namespace specific /sysfs
 * measurement file.
 *
 * \param fdptr		A pointer to the variable which will hold the
 *			file descriptor for the cartridge measurement
 *			file.
 *
 * \param enforce	A flag variable used to indicate whether or not
 *			the security domain should be placed in
 *			enforcement mode.
 *
 * \return		A boolean value is returned to indicate whether
 *			or not the the creation of the namespace was
 *			successful.  A false value indicates setup of
 *			the namespace was unsuccessful while a true
 *			value indicates the namespace is setup and
 *			ready to be measured.
 */

static _Bool setup_namespace(int *fdptr, _Bool enforce)

{
	_Bool retn = false;

	char fname[PATH_MAX];

	int fd;

	struct stat statbuf;


	/* Create an independent and sealed security event domain. */
	if ( unshare(CLONE_EVENTS) < 0 )
		ERR(goto done);

	if ( sys_config_domain(NULL, 0, IMA_EVENT_EXTERNAL) < 0 )
		ERR(goto done);

	if ( enforce ) {
		if ( sys_config_domain(NULL, 0, IMA_TE_ENFORCE) < 0 )
			ERR(goto done);
	}


	/* Drop the ability to modify the security domain. */
	if ( cap_drop_bound(CAP_TRUST) != 0 )
		ERR(goto done);


	/* Create the pathname to the event update file. */
	if ( stat("/proc/self/ns/events", &statbuf) < 0 )
		ERR(goto done);

	memset(fname, '\0', sizeof(fname));
	if ( snprintf(fname, sizeof(fname), SYSFS_UPDATES, \
		      (unsigned int) statbuf.st_ino) >= sizeof(fname) )
		ERR(goto done);
	if ( Debug )
		fprintf(Debug, "Update file: %s\n", fname);

	if ( (fd = open(fname, O_RDONLY)) < 0 )
		ERR(goto done);
	retn = true;


 done:
	if ( retn )
		*fdptr = fd;
	return retn;
}


/**
 * Private function.
 *
 * This function implements the show mode of the cboot utility.
 * This mode displays the set of Cartridges that are currently
 * provisioned on the host.
 *
 * \param root	A pointer to the buffer containing the root directory
 *		to be used to display the cartridges.
 *
 * \return	This function exits the program with a status code
 *		indicating whether or not the generation of the
 *		cartridge list was successful.  A non-zero return value
 *		indicates an error was encountered while a return
 *		value of zero indicates the list was successfully
 *		generated.
 */

static void * show_mode(CO(char *, root))

{
	char *p;

	int retn = 1;

	uint16_t lp;

	glob_t cartridges;

	String str = NULL;


	/* Generate the list of cartridges. */
	INIT(HurdLib, String, str, ERR(goto done));
	str->add(str, root);
	if ( !str->add(str, "/*") )
		ERR(goto done);

	if ( glob(str->get(str), 0, NULL, &cartridges) != 0 ) {
		fprintf(stderr, "Failed read of cartridge directory: %s\n", \
			root);
		goto done;
	}
	if ( cartridges.gl_pathc == 0 ) {
		fputs("No cartridges found:\n", stderr);
		goto done;
	}


	/* Iterate through and print the cartridges found .*/
	fprintf(stdout, "%s:\n", root);
	for (lp= 0; lp < cartridges.gl_pathc; ++lp) {
		str->reset(str);
		if ( !str->add(str, cartridges.gl_pathv[lp]) ) {
			fputs("Error processing cartridge list\n", stderr);
			goto done;
		}

		p = str->get(str);
		if ( (p = strrchr(str->get(str), '/')) == NULL )
			p = str->get(str);
		else
			++p;
		fprintf(stdout, "%s\n", p);
	}

	retn = 0;


 done:
	globfree(&cartridges);
	WHACK(str);

	exit(retn);
}


/**
 * Private function.
 *
 * This function is responsible for launching a software cartridge
 * in an independent measurement domain.  A pipe is established
 * between the parent process and the child that is used to return
 * the namespace specific events for injection into the security
 * co-processor.
 *
 * \param root		A pointer to the buffer containing the root
 *			directory to be used to display the cartridges.
 *
 * \param endpoint	A character pointer to the buffer containing
 *			the directory which holds the container to
 *			be executed.
 *
 * \param enforce	A flag used to indicate whether or not the
 *			security domain should be placed in enforcement
 *			mode.
 *
 * \return	A boolean value is returned to reflect the status of
 *		the launch.  A false value indicates an error was
 *		encountered while a true value indicates the cartridge
 *		was successfully launched.
 */

static _Bool fire_cartridge(CO(char *, cartridge), int *endpoint, \
			    _Bool enforce)

{
	_Bool retn = false;

	char *bundle,
	     bufr[512];

	int rc,
	    event_fd,
	    event_pipe[2];

	pid_t cartridge_pid;

	struct pollfd poll_data[1];

	String cartridge_dir = NULL;


	/* Create the name of the bundle directory. */
	INIT(HurdLib, String, cartridge_dir, ERR(goto done));
	cartridge_dir->add(cartridge_dir, CARTRIDGE_DIRECTORY);
	cartridge_dir->add(cartridge_dir, "/");
	if ( !cartridge_dir->add(cartridge_dir, cartridge) )
		ERR(goto done);
	bundle = cartridge_dir->get(cartridge_dir);


	/* Create the subordinate cartridge process. */
	if ( pipe(event_pipe) == -1 )
		ERR(goto done);

	Monitor_pid = fork();
	if ( Monitor_pid == -1 )
		ERR(goto done);


	/* Child process - create an independent namespace for this process. */
	if ( Monitor_pid == 0 ) {
		if ( Debug )
			fprintf(Debug, "Monitor process: %d\n", getpid());
		close(event_pipe[READ_SIDE]);

		if ( !setup_namespace(&event_fd, enforce) )
			exit(1);

		/* Fork again to run the cartridge. */
		cartridge_pid = fork();
		if ( cartridge_pid == -1 )
			exit(1);

		/* Child process - run the cartridge. */
		if ( cartridge_pid == 0 ) {
			execlp("runc", "runc", "run", "-b", bundle, \
			       cartridge, NULL);
			fputs("Cartridge execution failed.\n", stderr);
			exit(1);
		}

		/* Parent process - monitor for events. */
		poll_data[0].fd	    = event_fd;
		poll_data[0].events = POLLPRI;

		while ( true ) {
			if ( Signals.sigchild ) {
				if ( child_exited(cartridge_pid) ) {
					fputs("Cartridge spent.\n", stdout);
					close(event_fd);
					close(event_pipe[WRITE_SIDE]);
					_exit(0);
				}
			}

			memset(bufr, '\0', sizeof(bufr));

			rc = poll(poll_data, 1, -1);
			if ( rc < 0 ) {
				if ( errno == -EINTR ) {
					fputs("poll interrupted.\n", stderr);
					continue;
				}
			}

			if ( (poll_data[0].revents & POLLPRI) == 0 )
				continue;

			while ( true ) {
				rc = read(event_fd, bufr, sizeof(bufr));
				if ( rc == 0 )
					break;
				if ( rc < 0 ) {
					if ( errno != ENODATA ) {
						fputs("Fatal event read.\n", \
						      stderr);
						exit(1);
					}
					break;
				}
				if ( rc > 0 ) {
					write(event_pipe[WRITE_SIDE], bufr, \
					      rc);
					lseek(event_fd, 0, SEEK_SET);
				}
			}

			if ( lseek(event_fd, 0, SEEK_SET) < 0 ) {
				fputs("Seek error.\n", stderr);
				break;
			}
		}
	}


	/* Monitor parent process - return monitor fd. */
	close(event_pipe[WRITE_SIDE]);
	*endpoint = event_pipe[READ_SIDE];
	retn = true;


 done:
	WHACK(cartridge_dir);

	return retn;
}


/*
 * Program entry point begins here.
 */

extern int main(int argc, char *argv[])

{
	_Bool show	= false,
	      enforce	= false,
	      connected = false;

	char *p,
	     *debug	= NULL,
	     *map	= NULL,
	     *cartridge	= NULL,
	     bufr[1024];


	int opt,
	    fd	 = 0,
	    retn = 1;

	struct pollfd poll_data[2];

	struct sigaction signal_action;

	Buffer cmdbufr = NULL;

	LocalDuct mgmt = NULL;

	File infile = NULL;


	while ( (opt = getopt(argc, argv, "Sec:d:m:")) != EOF )
		switch ( opt ) {
			case 'S':
				show = true;
				break;
			case 'e':
				enforce = true;
				break;

			case 'c':
				cartridge = optarg;
				break;
			case 'd':
				debug = optarg;
				break;
			case 'm':
				map = optarg;
				break;
		}


	/* Execute cartridge display mode. */
	if ( show )
		show_mode(CARTRIDGE_DIRECTORY);


	if ( cartridge == NULL ) {
		fputs("No software cartridge specified.\n", stderr);
		goto done;
	}


	/* Handle a debug invocation. */
	if ( debug ) {
		if ( (Debug = fopen(debug, "w+")) == NULL ) {
			fputs("Cannot open debug file.\n", stderr);
			goto done;
		}
	}


	/* Setup signal handlers. */
	if ( sigemptyset(&signal_action.sa_mask) == -1 )
		ERR(goto done);

	signal_action.sa_flags = SA_SIGINFO | SA_NODEFER | SA_RESTART;
	signal_action.sa_sigaction = signal_handler;
	if ( sigaction(SIGINT, &signal_action, NULL) == -1 )
		goto done;
	if ( sigaction(SIGTERM, &signal_action, NULL) == -1 )
		goto done;
	if ( sigaction(SIGHUP, &signal_action, NULL) == -1 )
		goto done;
	if ( sigaction(SIGQUIT, &signal_action, NULL) == -1 )
		goto done;
	if ( sigaction(SIGSEGV, &signal_action, NULL) == -1 )
		goto done;
	if ( sigaction(SIGFPE, &signal_action, NULL) == -1 )
		goto done;
	if ( sigaction(SIGILL, &signal_action, NULL) == -1 )
		goto done;
	if ( sigaction(SIGBUS, &signal_action, NULL) == -1 )
		goto done;
	if ( sigaction(SIGTRAP, &signal_action, NULL) == -1 )
		goto done;
	if ( sigaction(SIGCHLD, &signal_action, NULL) == -1 )
		goto done;


	/* Initialize the security model. */
	INIT(NAAAIM, ISOidentity, Model, ERR(goto done));


	/* Load and seal a behavior map if specified. */
	if ( map != NULL ) {
		if ( Debug )
			fprintf(Debug, "Loading security state: %s\n", map);

		if ( !initialize_state(map) ) {
			fputs("Cannot initialize security state.\n", stderr);
			goto done;
		}
	}


	/* Setup the management socket. */
	INIT(NAAAIM, LocalDuct, mgmt, ERR(goto done));

	if ( !mgmt->init_server(mgmt) ) {
		fputs("Cannot set server mode.\n", stderr);
		goto done;
	}

	if ( !mgmt->init_port(mgmt, SOCKNAME) ) {
		fputs("Cannot initialize port.\n", stderr);
		goto done;
	}


	/* Launch the software cartridge. */
	if ( Debug )
		fprintf(Debug, "Primary process: %d\n", getpid());

	if ( !fire_cartridge(cartridge, &fd, enforce) )
		ERR(goto done);
	poll_data[0].fd	    = fd;
	poll_data[0].events = POLLIN;

	if ( !mgmt->get_socket(mgmt, &poll_data[1].fd) ) {
		fputs("Error setting up polling data.\n", stderr);
		goto done;
	}
	poll_data[1].events = POLLIN;


	/* Dispatch loop. */
	if ( Debug ) {
		fprintf(Debug, "%d: Calling event loop\n", getpid());
		fprintf(Debug, "descriptor 1: %d, descriptor 2: %d\n", \
			poll_data[0].fd, poll_data[1].fd);
	}

	INIT(HurdLib, Buffer, cmdbufr, ERR(goto done));

	opt = 0;
	while ( 1 ) {
		if ( Debug )
			fprintf(Debug, "\n%d: Poll cycle: %d\n", getpid(), \
				++opt);

		retn = poll(poll_data, 2, -1);
		if ( retn < 0 ) {
			if ( Signals.stop )
				break;
			if ( Signals.sigchild ) {
				if ( !child_exited(Monitor_pid) )
					continue;
				fputs("Cartridge exited.\n", stdout);
				goto done;
			}
			goto done;
		}
		if ( retn == 0 ) {
			if ( Debug )
				fputs("Poll timeout.\n", Debug);
			continue;
		}

		if ( Debug )
			fprintf(Debug, "Events: %d, Data poll=%0x, "	\
				"Mgmt poll=%0x\n", retn,		\
				poll_data[0].revents, poll_data[1].revents);

		if ( poll_data[0].revents & POLLHUP ) {
			if ( Signals.stop ) {
				fputs("Cartridge stopped.\n", stdout);
				goto done;
			}
			if ( Signals.sigchild ) {
				if ( !child_exited(Monitor_pid) )
					continue;
				fputs("Cartridge exited.\n", stdout);
				goto done;
			}
		}

		if ( poll_data[0].revents & POLLIN ) {
			p = bufr;
			memset(bufr, '\0', sizeof(bufr));
			while ( 1 ) {
				retn = read(fd, p, 1);
				if ( retn < 0 ) {
					if ( errno != ENODATA )
						fprintf(stderr, "Have "	    \
							"error: retn=%d, "  \
							"error=%s\n", retn, \
							strerror(errno));
				}
				if ( *p != '\n' ) {
					++p;
					continue;
				}
				else
					*p = '\0';
				if ( Debug )
					fprintf(Debug,			  \
						"Processing event: %s\n", \
						bufr);
				if ( !process_event(bufr) )
					ERR(goto done);
				break;
			}
		}

		if ( poll_data[1].revents & POLLIN ) {
			if ( !connected ) {
				if ( Debug )
					fputs("Have socket connection.\n", \
					      Debug);

				if ( !mgmt->accept_connection(mgmt) )
					ERR(goto done);
				if ( !mgmt->get_fd(mgmt, &poll_data[1].fd) )
					ERR(goto done);
				connected = true;
				continue;
			}
			if ( !mgmt->receive_Buffer(mgmt, cmdbufr) )
				continue;
			if ( mgmt->eof(mgmt) ) {
				if ( Debug )
					fputs("Terminating management.\n", \
					      Debug);
				mgmt->reset(mgmt);
				if ( !mgmt->get_socket(mgmt, \
						       &poll_data[1].fd) )
					ERR(goto done);
				connected = false;
				continue;
			}

			if ( !process_command(mgmt, cmdbufr) )
				ERR(goto done);
			cmdbufr->reset(cmdbufr);
		}
	}


 done:
	WHACK(cmdbufr);
	WHACK(mgmt);
	WHACK(infile);
	WHACK(Model);

	if ( fd > 0 )
		close(fd);

	return retn;
}

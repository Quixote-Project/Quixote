/** \file
 *
 * This file implements a utility for running and managing software
 * stacks in an independent hardware masurement domain.  After creating an
 * independent measurement domain the utility forks and then executes
 * the boot of the cartridge in a subordinate process.  The parent
 * process monitors the following file:
 *
 * /sys/fs/iso-identity/update
 *
 * The measurement update events are transmitted to sancho the
 * hardware security co-processor.  Based on feedback from the
 * processor the processor eliciting the event is either killed or
 * woken with its bad actor status bit set so that its behavior can
 * be disciplined by a linux security module.
 *
 * For measurement domain updates.
 *
 * The domain is managed through a UNIX domain socket which is created
 * in the following location:
 *
 * /var/run/cboot.PIDNUM
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


/**
 * Private function.
 *
 * This function is responsible for interpreting the measurement
 * event generated by the kernel.  It does this be iterating over
 * the the defined commands and then switching execution based
 * on the enumeration type of the event.
 *
 * \param event		A pointer to the character buffer containing
 *			the ASCII encoded event.
 *
 * \return		A boolean value is returned to indicate whether
 *			or not processing of the event was successful.  A
 *			false value indicates a failure in event
 *			processing while a true value indicates that
 *			event processing has succeeded.
 */

static _Bool process_event(CO(TTYduct, duct), const char * const event)

{
	_Bool retn = false;

	char *bp;

	pid_t pid;

	struct sancho_cmd_definition *cp;

	Buffer bufr = NULL;

	static const char *discipline  = "DISCIPLINE ",
			  *release     = "RELEASE ",
			  *measurement = "measurement " ;


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
	if ( cp->command > ai_event ) {
		fprintf(stderr, "Unexpected event: %s\n", event);
		goto done;
	}


	/* Dispatch the event. */
	INIT(HurdLib, Buffer, bufr, ERR(goto done));
	if ( !bufr->add(bufr, (unsigned char *) event, strlen(event) + 1) )
		ERR(goto done);

	if ( !duct->send_Buffer(duct, bufr) ) {
		fputs("Error sending command.\n", stderr);
		goto done;
	}

	bufr->reset(bufr);
	if ( !duct->receive_Buffer(duct, bufr) ) {
		fputs("Error receiving command.\n", stderr);
		goto done;
	}

	if ( Debug )
		fprintf(Debug, "Sancho says: %s\n", bufr->get(bufr));


	/* Check event return, OK if other then an exchange event. */
	bp = (char *) bufr->get(bufr);

	if ( cp->command != exchange_event ) {
		if ( strncmp(bp, "OK", 2) == 0 )
			retn = true;
		goto done;
	}


	/* Extract the PID from the exchange event. */
	if ( (bp = strchr(bp, ' ')) == NULL )
		goto done;

	pid = strtoll(++bp, NULL, 10);
	if ( errno == ERANGE )
		ERR(goto done);

	bp = (char *) bufr->get(bufr);

	if ( strncmp(bp, discipline, strlen(discipline)) == 0 ) {
		if ( sys_config_actor(pid, DISCIPLINE_ACTOR) < 0 ) {
			fprintf(stderr, "Failed discipline: errno=%d, "\
				"error=%s\n", errno, strerror(errno));
		}
		else {
			if ( Debug )
				fprintf(Debug, "Disciplined: %d\n", pid);
		}
		retn = true;
	}

	if ( strncmp(bp, release, strlen(release)) == 0 ) {
		if ( sys_config_actor(pid, RELEASE_ACTOR) < 0 ) {
			fprintf(stderr, "Failed release: errno=%d, " \
				"error=%s\n", errno, strerror(errno));
		}
		else {
			if ( Debug )
				fprintf(Debug, "Released: %d\n", pid);
		}
		retn = true;
	}


 done:
	WHACK(bufr);

	return retn;
}


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


/**
 * Private function.
 *
 * This function implements the processing of a command from the
 * cartridge management utility.  This command comes in the form
 * of a binary encoding of the desired command to be run.
 *
 * \param mgmt		The socket object used to communicate with
 *			the cartridge management instance.
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

static _Bool process_command(CO(TTYduct, duct), CO(LocalDuct, mgmt), \
			     CO(Buffer, cmdbufr))

{
	_Bool retn = false;

	char *cmd;

	int *cp;

	static const char *seal_cmd	   = "seal",
			  *measurement_cmd = "show measurement",
			  *state_cmd	   = "show state",
			  *cellular_cmd	   = "enable cellular";


	if ( cmdbufr->size(cmdbufr) != sizeof(int) )
		ERR(goto done);

	cp = (int *) cmdbufr->get(cmdbufr);
	if ( (*cp < 1) || (*cp > sancho_cmds_max) )
		ERR(goto done);
	cmd = Sancho_cmd_list[*cp - 1].syntax;

	if ( Debug )
		fprintf(Debug, "Processing managment cmd: %s\n", cmd);

	switch ( *cp ) {
		case seal_event:
			cmdbufr->reset(cmdbufr);
			if ( !cmdbufr->add(cmdbufr,		       \
					   (unsigned char *) seal_cmd, \
					   strlen(seal_cmd) + 1) )
			       ERR(goto done);
			if ( !duct->send_Buffer(duct, cmdbufr) )
				ERR(goto done);

			cmdbufr->reset(cmdbufr);
			if ( !duct->receive_Buffer(duct, cmdbufr) )
				ERR(goto done);
			if ( !mgmt->send_Buffer(mgmt, cmdbufr) )
				ERR(goto done);

			retn = true;
			break;

		case show_measurement:
			cmdbufr->reset(cmdbufr);
			if ( !cmdbufr->add(cmdbufr,			      \
					   (unsigned char *) measurement_cmd, \
					   strlen(measurement_cmd) + 1) )
				ERR(goto done);
			if ( !duct->send_Buffer(duct, cmdbufr) )
				ERR(goto done);

			cmdbufr->reset(cmdbufr);
			if ( !duct->receive_Buffer(duct, cmdbufr) )
				ERR(goto done);
			if ( !mgmt->send_Buffer(mgmt, cmdbufr) )
				ERR(goto done);

			retn = true;
			break;

		case show_state:
			cmdbufr->reset(cmdbufr);
			if ( !cmdbufr->add(cmdbufr,			\
					   (unsigned char *) state_cmd, \
					   strlen(state_cmd) + 1) )
				ERR(goto done);
			if ( !duct->send_Buffer(duct, cmdbufr) )
				ERR(goto done);

			cmdbufr->reset(cmdbufr);
			if ( !duct->receive_Buffer(duct, cmdbufr) )
				ERR(goto done);
			if ( !mgmt->send_Buffer(mgmt, cmdbufr) )
				ERR(goto done);

			retn = true;
			break;

		case show_trajectory:
			retn = send_list(duct, mgmt, cmdbufr, \
					 "show trajectory");
			break;

		case show_forensics:
			retn = send_list(duct, mgmt, cmdbufr, \
					 "show forensics");
			break;

		case show_contours:
			retn = send_list(duct, mgmt, cmdbufr, \
					 "show contours");
			break;

		case show_events:
			retn = send_list(duct, mgmt, cmdbufr, \
					 "show events");
			break;

		case enable_cell:
			cmdbufr->reset(cmdbufr);
			if ( !cmdbufr->add(cmdbufr,			   \
					   (unsigned char *) cellular_cmd, \
					   strlen(cellular_cmd) + 1) )
			       ERR(goto done);
			if ( !duct->send_Buffer(duct, cmdbufr) )
				ERR(goto done);

			cmdbufr->reset(cmdbufr);
			if ( !duct->receive_Buffer(duct, cmdbufr) )
				ERR(goto done);
			if ( !mgmt->send_Buffer(mgmt, cmdbufr) )
				ERR(goto done);

			retn = true;
			break;

		case sancho_reset:
			cmdbufr->reset(cmdbufr);
			if ( !cmdbufr->add(cmdbufr, (void *) cmd,
					   strlen(cmd) + 1) )
				ERR(goto done);
			if ( !duct->send_Buffer(duct, cmdbufr) )
				ERR(goto done);

			cmdbufr->reset(cmdbufr);
			if ( !duct->receive_Buffer(duct, cmdbufr) )
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
 * \param cmdbufr	The object containing the command to be
 *			processed.
 *
 * \return		A boolean value is returned to indicate whether
 *			or not the command was processed.  A false value
 *			indicates the processing of commands should be
 *			terminated while a true value indicates an
 *			additional command cycle should be processed.
 */

static _Bool initialize_state(CO(TTYduct, duct), char *mapfile)

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

		if ( !process_event(duct, inbufr) )
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
 * \return		A boolean value is returned to indicate whether
 *			or not the the creation of the namespace was
 *			successful.  A false value indicates setup of
 *			the namespace was unsuccessful while a true
 *			value indicates the namespace is setup and
 *			ready to be measured.
 */

static _Bool setup_namespace(int *fdptr)

{
	_Bool retn = false;

	char fname[PATH_MAX];

	int fd;

	struct stat statbuf;


	/* Create an independent security event namespace. */
	if ( unshare(CLONE_EVENTS) < 0 )
		ERR(goto done);

	if ( sys_config_domain(NULL, 0, IMA_EVENT_EXTERNAL) < 0 )
		ERR(goto done);


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
 * \return	A boolean value is returned to reflect the status of
 *		the launch.  A false value indicates an error was
 *		encountered while a true value indicates the cartridge
 *		was successfully launched.
 */

static _Bool fire_cartridge(CO(char *, cartridge), int *endpoint)

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

		if ( !setup_namespace(&event_fd) )
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


/**
 * Private function.
 *
 * This function is responsible for issueing a reset request to the
 * Sancho micro-controller implementation.
 *
 * \param duct		A pointer to the object being used to communicate
 *			with the micro-controller.
 *
 * \param bufr		The object which will be used to hold the command
 *			to be sent.
 *
 * \return	No return value is defined.
 */

static void send_reset(CO(TTYduct, duct), CO(Buffer, bufr))

{

	static unsigned char cmd[] = "reset";


	if ( Debug )
		fputs("Sending reset.\n", Debug);

	bufr->reset(bufr);
	if ( bufr->add(bufr, cmd, sizeof(cmd)) )
		duct->send_Buffer(duct, bufr);

	return;
}


/*
 * Program entry point begins here.
 */

extern int main(int argc, char *argv[])

{
	_Bool show	= false,
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

	TTYduct Duct = NULL;


	while ( (opt = getopt(argc, argv, "Sc:d:m:")) != EOF )
		switch ( opt ) {
			case 'S':
				show = true;
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


	/* Open a connection to the co-processor. */
	INIT(NAAAIM, TTYduct, Duct, ERR(goto done));
	if ( !Duct->init_device(Duct, "/dev/ttyACM0") )
		ERR(goto done);


	/* Load and seal a behavior map if specified. */
	if ( map != NULL ) {
		if ( Debug )
			fprintf(Debug, "Loading security state: %s\n", map);

		if ( !initialize_state(Duct, map) ) {
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

	if ( !fire_cartridge(cartridge, &fd) )
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
			if ( Signals.sigchild ) {
				if ( !child_exited(Monitor_pid) )
					continue;
				fputs("Cartridge exited.\n", stdout);
				send_reset(Duct, cmdbufr);
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
				if ( !process_event(Duct, bufr) )
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

			if ( !process_command(Duct, mgmt, cmdbufr) )
				ERR(goto done);
			cmdbufr->reset(cmdbufr);
		}
	}


 done:
	WHACK(cmdbufr);
	WHACK(mgmt);
	WHACK(infile);
	WHACK(Duct);

	if ( fd > 0 )
		close(fd);

	return retn;
}

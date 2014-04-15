/** \file
 * This file implements methods for configuring an IPsec tunnel.
 */

/**************************************************************************
 * (C)Copyright 2014, The Open Hurderos Foundation. All rights reserved.
 *
 * Please refer to the file named COPYING in the top of the source tree
 * for licensing information.
 **************************************************************************/

/* Local defines. */
/* Path to the setkey utility. */
#define SETKEY "/usr/local/sbin/setkey"


/* Include files. */
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <Origin.h>
#include <HurdLib.h>
#include <Buffer.h>
#include <String.h>

#include "NAAAIM.h"
#include "IPsec.h"


/* Verify library/object header file inclusions. */
#if !defined(NAAAIM_LIBID)
#error Library identifier not defined.
#endif

#if !defined(NAAAIM_IPsec_OBJID)
#error Object identifier not defined.
#endif

/* Macro to extract state state information into the specified variable. */
#define STATE(var) CO(IPsec_State, var) = this->state


/** IPsec private state information. */
struct NAAAIM_IPsec_State
{
	/* The root object. */
	Origin root;

	/* Library identifier. */
	uint32_t libid;

	/* Object identifier. */
	uint32_t objid;

	/* Object status. */
	_Bool poisoned;
};


/**
 * Internal private method.
 *
 * This method is responsible for initializing the NAAAIM_IPsec_State
 * structure which holds state information for each instantiated object.
 *
 * \param S	A pointer to the object containing the state information
 *		which is to be initialized.
 */

static void _init_state(CO(IPsec_State, S))

{
	S->libid = NAAAIM_LIBID;
	S->objid = NAAAIM_IPsec_OBJID;

	S->poisoned = false;

	return;
}


/**
 * Private function.
 *
 * This function implements executing the IPsec command which has been
 * encoded into the supplied String object.
 *
 * \param cmd	The ASCII text of the command to be executed.
 *
 * \return	A boolean value is used to indicate the status of the
 *		execution of the command.  Failure is indicated by a
 *		false value while success is designated by a true
 *		value.
 */

static _Bool _execute(CO(String, cmd))

{
	_Bool retn = false;

	FILE *setkey;

	/* Arguement verification. */
	if ( (cmd == NULL) || cmd->poisoned(cmd) )
		goto done;

	/* Open a pipe to the setkey binary and output command. */
	if ( (setkey = popen(SETKEY " -c", "w")) == NULL )
		goto done;
	fprintf(setkey, "%s\n", cmd->get(cmd));
	if ( pclose(setkey) != -1 )
		retn = true;

 done:
	return retn;
}


/**
 * Private function.
 *
 * This function adds the contents of a specified Buffer object to
 * a provided String object in the form of hexadecimal constants.
 *
 * \param input		A Buffer object containing the binary values to
 *			be encoded.
 *
 * \param output	A String object which will receive the hexadecimal
 *			values.
 *
 * \return		No return value is specified.
 */

static void add_hex_Buffer(CO(Buffer, input), CO(String, output))

{
	unsigned char *p;

	char bufr[3];

	size_t cnt,
	       lp = 0;


	/* Arguement validation. */
	if ( (input == NULL) || input->poisoned(input) )
		goto done;
	if ( (output == NULL) || output->poisoned(output) )
		goto done;

	/*
	 * Convert the bytes in the buffer into their hexadecimal
	 * equivalents.
	 */
	p   = input->get(input);
	cnt = input->size(input);
	for (lp= 0; lp < cnt; ++lp) {
		snprintf(bufr, sizeof(bufr), "%02x", *p);
		output->add(output, bufr);
		++p;
	}

 done:
	return;
}


/**
 * Private function.
 *
 * This function emits a single stanza of a security association.  It
 * is designed to be called twice to setup the two sides of the mirror.
 *
 * \param cmd		The cmd which is being created for submission.
 *
 * \param ip1		The first IP in the association.
 *
 * \param ip2		The second IP in the association.
 *
 * \param spi		The Security Policy Index.
 *
 * \param enc_alg	The encryption algorithm specification.
 *
 * \param enc_key	The encryption key.
 *
 * \param mac_alg	The authentication algorithm specification.
 *
 * \param mac_key	The authentication key.
 */

void _emit_sa(CO(String, cmd), CO(char *, ip1), CO(char *, ip2), int spi,    \
	      CO(char *, enc_alg), CO(Buffer, enc_key), CO(char *, mac_alg), \
	      CO(Buffer, mac_key) )

{
	char bufr[256];

	int sz;


	sz = snprintf(bufr, sizeof(bufr), "add %s %s ", ip1, ip2);
	if ( sz >= sizeof(bufr) )
		goto done;
	cmd->add(cmd, bufr);

	sz = snprintf(bufr, sizeof(bufr), "esp 0x%x -m tunnel -E %s 0x", \
		      spi, enc_alg);
	if ( sz >= sizeof(bufr) )
		goto done;
	cmd->add(cmd, bufr);

	add_hex_Buffer(enc_key, cmd);

	sz = snprintf(bufr, sizeof(bufr), " -A %s 0x", mac_alg);
	if ( sz >= sizeof(bufr) )
		goto done;
	cmd->add(cmd, bufr);

	add_hex_Buffer(mac_key, cmd);

	cmd->add(cmd, ";\n");


 done:
	return;
}

		
/**
 * External public method.
 *
 * This function is responsible for configuring a security association
 * between the specified local and remote address.  This association
 * will secure the endpoints of the IPsec tunnel.
 *
 * \parm this		The IP security object being configured.
 *
 * \param local		The local IP address for the association.
 *
 * \param remote	The remote IP address for the association.
 *
 * \param spi		The security parameter index
 *
 * \param enc_alg   	A name of the encryption algorithm to be used.
 *
 * \param enc_key	The key/nonce value to be used for the encryption
 *			algorithm.
 *
 * \param mac_alg	The name of the message authentication code (MAC) to
 *			be used for the association.
 *
 * \param mac_key	The key to be used for the MAC.
 *			
 *
 * \return	A boolean value is used to indicate the status of setting
 *		up the association.  Failure is denoted by returning
 *		a false value while success is indicated by a true value.
 */

static _Bool setup_sa(CO(IPsec, this), CO(char *, local), CO(char *, remote), \
		      const int spi,					      \
		      CO(char *, enc_alg), CO(Buffer, enc_key),		      \
		      CO(char *, mac_alg), CO(Buffer, mac_key))

{
	STATE(S);

	_Bool retn = false;

	String cmd = NULL;


	/* Arguement validation. */
	if ( (enc_key == NULL) || enc_key->poisoned(enc_key) )
		goto done;
	if ( (mac_key == NULL) || mac_key->poisoned(mac_key) )
		goto done;

	INIT(HurdLib, String, cmd, goto done);
	_emit_sa(cmd, local, remote, spi, enc_alg, enc_key, mac_alg, mac_key);
	_emit_sa(cmd, remote, local, spi, enc_alg, enc_key, mac_alg, mac_key);
	fprintf(stdout, "%s: SA command\n", __func__);
	cmd->print(cmd);

	if ( cmd->poisoned(cmd) )
		goto done;
	if ( _execute(cmd) )
		retn = true;

 done:
	if ( !retn )
		S->poisoned = true;

	WHACK(cmd);

	return retn;
}


/**
 * Private function.
 *
 * This function emits a single stanza of a security policy.  It
 * is designed to be called twice to setup the two sides of the
 * tunnel.
 *
 * \param cmd		The cmd which is being created for submission.
 *
 * \parm direction	The direction to be applied to this policy.
 *
 * \param ip1		The first IP address in the policy.
 *
 * \param ip2		The second IP address in the policy.
 *
 * \param net1		The first address in the policy.
 *
 * \param net2		The second address in the policy.
 *
 * \param cidr		The CIDR mask to be applied to the network
 *			endpoints.
 *
 */

void _emit_spd(CO(String, cmd), CO(char *, direction), CO(char *, ip1), \
	       CO(char *, ip2), CO(char *, net1), CO(char *, net2),	\
	       const int cidr)

{
	char bufr[256];

	int sz;


	sz = snprintf(bufr, sizeof(bufr), "spdadd %s/%d %s/%d ", net1, cidr, \
		      net2, cidr);
	if ( sz >= sizeof(bufr) )
		goto done;
	cmd->add(cmd, bufr);

	sz = snprintf(bufr, sizeof(bufr), "any -P %s ipsec \n", direction);
	if ( sz >= sizeof(bufr) )
		goto done;
	cmd->add(cmd, bufr);

	sz = snprintf(bufr, sizeof(bufr), "\tesp/tunnel/%s-%s/require", \
		      ip1, ip2);
	if ( sz >= sizeof(bufr) )
		goto done;
	cmd->add(cmd, bufr);

	cmd->add(cmd, ";\n");


 done:
	return;
}


/**
 * External public method.
 *
 * This function is responsible for creating a Security Policy Database
 * (SPD) entry.  An SPD entry specifies what type of security policy is
 * to be applied to given endpoints.
 *
 * This method is used to implement an IPsec tunnel (ESP) mode between
 * two networks using a specified set of network endpoints.
 *
 * \parm this		The IP security object being configured.
 *
 * \param local		The local IP address for the association.
 *
 * \param remote	The remote IP address for the association.
 *
 * \param local_net	The address of the local network which is to
 *			be tunnelled to the remote network.
 *
 * \param remote_net	The address of the remote network which the
 *			local network is to be tunnelled to.
 *
 * \param netmask	The netmask to be used for the network
 *			endpoints.
 *
 * \return	A boolean value is used to indicate the status of setting
 *		up the policy element.  Failure is denoted by returning
 *		a false value while success is indicated by a true value.
 */

static _Bool setup_spd(CO(IPsec, this), CO(char *, local),	  \
		       CO(char *, remote), CO(char *, local_net), \
		       CO(char *, remote_net), CO(char *, netmask))

{
	_Bool retn = false;
	
	unsigned int cidr = 0;

	in_addr_t mask = inet_addr(netmask);

	String cmd = NULL;


	/* Calculate CIDR count. */
	while ( mask ) {
		if ( mask & 0x1 )
			cidr += 1;
		mask >>= 1;
	}

	INIT(HurdLib, String, cmd, goto done);
	_emit_spd(cmd, "out", local, remote, local_net, remote_net, cidr);
	_emit_spd(cmd, "in", remote, local, remote_net, local_net, cidr);
	fprintf(stdout, "%s: SPD command\n", __func__);
	cmd->print(cmd);

	if ( cmd->poisoned(cmd) )
		goto done;
	if ( _execute(cmd) )
		retn = true;


 done:
	WHACK(cmd);

	return retn;
}


/**
* External public method.
*
* This method implements a check to determine whether or not a
* Security Parameter Index (SPI) value is in use.  It does this by
* running the setkey in 'dump' mode and parsing the output for
* spi= directives.
*
* Since this object is querying a system global value which may
* be modified at any time any locking is considered to be the
* responsibility of the caller.
*
* \param this	The object which will be requesting the SPI check.
*
* \param spi	The SPI value which is to be verified.
*
* \return	If the specified SPI value is not currently active a
*		false value will be returned.  If the SPI is in use a
*		true value is returned.  If an error is encountered
*		the object is poisoned so there is a method of
*		deteting whether or not the operation was successful
*		from an operational perspective.
*/

static _Bool have_spi(CO(IPsec, this), const uint32_t spi)

{
	STATE(S);

	_Bool found = false,
	      retn  = false;

	char *p,
	     *pend,
	     bufr[256];

	long int value;

	const char * const tag = "spi=";

	FILE *setkey = NULL;


	/* Open a pipe to the setkey binary and output command. */
	if ( (setkey = popen(SETKEY " -D", "r")) == NULL )
		goto done;

	/* Parse the output for fields with spi= values. */
	while ( fgets(bufr, sizeof(bufr), setkey) != NULL ) {
		if ( (p = strchr(bufr, '\n')) != NULL )
			*p = '\0';
		if ( (p = strstr(bufr, tag)) == NULL )
			continue;

		/* Found spi= tag, parse foward to start of number. */
		while ( (*p != '\0') && (*p != '=') )
			++p;

		/* Parse forward until the end of the number. */
		pend = ++p;
		while ( (*pend != '\0') && (*pend != '(') )
			++pend;
		*pend = '\0';

		/* Convert the number into an integer and check value. */
		value = strtol(p, NULL, 10);
		if ( errno == ERANGE )
			goto done;
		if ( value == spi ) {
			found = true;
			break;
		}
	}
	retn = true;

 done:
	if ( (setkey != NULL) && (pclose(setkey) == -1) )
	     retn = false;
	if ( !retn ) {
		found	    = false;
		S->poisoned = true;
	}

	return found;
}


/**
 * External public method.
 *
 * This method implements the return of the object status.
 *
 * \param this	A pointer to the object whose status is to be
 *		returned.
 *
 * \return	The return value will be either true or false based
 *		on the poisoned status in the variable.
 */

static _Bool poisoned(CO(IPsec, this))

{
	STATE(S);

	return S->poisoned;
}


/**
 * External public method.
 *
 * This method implements a destructor for a IPsec object.
 *
 * \param this	A pointer to the object which is to be destroyed.
 */

static void whack(CO(IPsec, this))

{
	STATE(S);

	S->root->whack(S->root, this, S);
	return;
}

	
/**
 * External constructor call.
 *
 * This function implements a constructor call for a IPsec object.
 *
 * \return	A pointer to the initialized IPsec.  A null value
 *		indicates an error was encountered in object generation.
 */

extern IPsec NAAAIM_IPsec_Init(void)

{
	auto Origin root;

	auto IPsec this = NULL;

	auto struct HurdLib_Origin_Retn retn;


	/* Get the root object. */
	root = HurdLib_Origin_Init();

	/* Allocate the object and internal state. */
	retn.object_size  = sizeof(struct NAAAIM_IPsec);
	retn.state_size   = sizeof(struct NAAAIM_IPsec_State);
	if ( !root->init(root, NAAAIM_LIBID, NAAAIM_IPsec_OBJID, &retn) )
		return NULL;
	this	    	  = retn.object;
	this->state 	  = retn.state;
	this->state->root = root;

	/* Initialize aggregate objects. */

	/* Initialize object state. */
	_init_state(this->state);

	/* Method initialization. */
	this->setup_sa  = setup_sa;
	this->setup_spd = setup_spd;

	this->have_spi = have_spi;
	this->poisoned = poisoned;
	this->whack    = whack;

	return this;
}

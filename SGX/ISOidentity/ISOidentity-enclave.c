#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>

#include <HurdLib.h>
#include <Buffer.h>
#include <String.h>
#include <SHA256.h>

#include "ISOidentity-interface.h"
#include "regex.h"
#include "ContourPoint.h"
#include "ExchangeEvent.h"
#include "ISOidentity.h"


/**
 * The model being implemented.
 */
ISOidentity Model = NULL;


/**
 * External ECALL 0.
 *
 * This method implements the initialization of the ISOidentity model
 * inside of the enclave.
 *
 * \return	A boolean value is used to indicate whether or not
 *		initialization of the model succeeded.  A false value
 *		indicates initialization failed while a true value
 *		indicates the enclave is ready to receive measurements.
 */

_Bool init_model(void)

{
	_Bool retn = false;


	INIT(NAAAIM, ISOidentity, Model, ERR(goto done));
	retn = true;


 done:

	return retn;
}


/**
 * External ECALL 1.
 *
 * This method implements adding updates to the ISOidentity model
 * being implemented inside an enclave.
 *
 * \return	A boolean value is used to indicate whether or not
 *		the update to the model had succeeded.  A false value
 *		indicates the update had failed while a true value
 *	        indicates the enclave model had been updated.
 */

_Bool update_model(char *update)

{
	_Bool updated,
	      discipline,
	      retn = false;

	String input = NULL;

	ExchangeEvent event = NULL;


	/* Initialize a string object with the model update. */
	INIT(HurdLib, String, input, ERR(goto done));
	if ( !input->add(input, update) )
		ERR(goto done);


	/* Parse and measure the event. */
	INIT(NAAAIM, ExchangeEvent, event, ERR(goto done));
	if ( !event->parse(event, input) )
		ERR(goto done);
	if ( !event->measure(event) )
		ERR(goto done);


	/* Update the model. */
	if ( !Model->update(Model, event, &updated, &discipline) )
		ERR(goto done);
	if ( !updated )
		WHACK(event);

	retn = true;


 done:
	WHACK(input);

	return retn;
}


/**
 * External ECALL 2.
 *
 * This method implements sealing the current state of the model.
 *
 * \return	No return value is defined.
 */

void seal_model(void)

{
	Model->seal(Model);
	return;
}


/**
 * External ECALL 3.
 *
 * This method implements dumping the current state of the model.
 *
 * \return	No return value is defined.
 */

void dump_model(void)

{
	fputs("Events:\n", stdout);
	Model->dump_events(Model);

	fputs("Contours:\n", stdout);
	Model->dump_contours(Model);
	fputc('\n', stdout);

	fputs("Forensics:\n", stdout);
	Model->dump_forensics(Model);

	return;
}
